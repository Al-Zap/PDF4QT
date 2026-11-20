// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pdfcompiler.h"
#include "pdfcms.h"
#include "pdfprogress.h"
#include "pdfexecutionpolicy.h"
#include "pdftextlayoutgenerator.h"
#include "pdfdrawspacecontroller.h"

#include <QCache>
#include <QtConcurrent/QtConcurrent>

#include "pdfdbgheap.h"

#include <execution>
#include <limits>

namespace pdf
{

PDFAsynchronousPageCompilerWorkerThread::PDFAsynchronousPageCompilerWorkerThread(PDFAsynchronousPageCompiler* parent) :
    QThread(parent),
    m_compiler(parent),
    m_mutex(&m_compiler->m_mutex),
    m_waitCondition(&m_compiler->m_waitCondition)
{

}

void PDFAsynchronousPageCompilerWorkerThread::run()
{
    QMutexLocker locker(m_mutex);
    while (!isInterruptionRequested())
    {
        std::vector<PDFAsynchronousPageCompiler::CompileTask> tasks = m_compiler->pickTasksForCompilation();

        if (tasks.empty())
        {
            // Jakub Melka: new tasks and the interruption request are both signalled
            // with the mutex locked, so the check above and the wait form one atomic
            // step - no wake up can be lost (otherwise stop() could wait forever).
            // The timeout is just a safety net, the loop checks everything again.
            m_waitCondition->wait(locker.mutex(), QDeadlineTimer(500));
            continue;
        }

        locker.unlock();

        // Perform page compilation
        auto proxy = m_compiler->getProxy();
        proxy->getFontCache()->setCacheShrinkEnabled(this, false);

        auto compilePage = [this, proxy](PDFAsynchronousPageCompiler::CompileTask& task) -> PDFPrecompiledPage
        {
            PDFPrecompiledPage compiledPage;

            // Jakub Melka: task can be cancelled while it is waiting in the thread
            // pool queue - in that case we do not start the compilation at all.
            if (task.isCancelled())
            {
                return compiledPage;
            }

            PDFPageCompilerOperationControl operationControl(m_compiler, task.cancelFlag);
            PDFCMSPointer cms = proxy->getCMSManager()->getCurrentCMS();
            PDFRenderer renderer(proxy->getDocument(), proxy->getFontCache(), proxy->getIccProfileCache(), cms.data(), proxy->getOptionalContentActivity(), proxy->getFeatures(), proxy->getMeshQualitySettings());
            renderer.setOperationControl(&operationControl);
            renderer.compile(&task.precompiledPage, task.pageIndex);
            task.finished = !operationControl.isOperationCancelled();

            if (!task.finished || task.isCancelled())
            {
                return compiledPage;
            }

            // The page is published immediately, it does not wait for the other pages
            // of the batch - the visible page would wait for the prefetched pages, which
            // can be compiled much longer. We must check, that the task is still present
            // in the task map - it can be cancelled and erased while we were compiling
            // it (for example, page scrolled out of the viewport).
            bool isWritten = false;
            {
                QMutexLocker taskLocker(m_mutex);
                auto it = m_compiler->m_tasks.find(task.pageIndex);
                if (it != m_compiler->m_tasks.end() && !it->second.finished)
                {
                    isWritten = true;
                    task.isCancellable = it->second.isCancellable;
                    task.isPrefetch = it->second.isPrefetch;
                    it->second = std::move(task);
                }
            }

            if (isWritten)
            {
                // Signal is emitted without the mutex - if direct
                // connection is applied, this can lead to deadlock.
                Q_EMIT pageCompiled();
            }

            return compiledPage;
        };
        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, tasks.begin(), tasks.end(), compilePage);

        proxy->getFontCache()->setCacheShrinkEnabled(this, true);

        // Compiled pages are already written by the tasks, relock
        // the mutex to pick new tasks.
        locker.relock();
    }
}

PDFAsynchronousPageCompiler::PDFAsynchronousPageCompiler(PDFDrawWidgetProxy* proxy) :
    BaseClass(proxy),
    m_proxy(proxy),
    m_cache(new QCache<PDFInteger, PDFPrecompiledPagePointer>())
{
    m_cache->setMaxCost(128 * 1024 * 1024);
}

PDFAsynchronousPageCompiler::~PDFAsynchronousPageCompiler()
{
    stop(true);

    delete m_cache;
    m_cache = nullptr;
}

bool PDFAsynchronousPageCompiler::isOperationCancelled() const
{
    return m_state == State::Stopping;
}

void PDFAsynchronousPageCompiler::start()
{
    switch (m_state)
    {
        case State::Inactive:
        {
            Q_ASSERT(!m_thread);
            m_state = State::Active;
            m_thread = new PDFAsynchronousPageCompilerWorkerThread(this);
            connect(m_thread, &PDFAsynchronousPageCompilerWorkerThread::pageCompiled, this, &PDFAsynchronousPageCompiler::onPageCompiled);
            m_thread->start();
            break;
        }

        case State::Active:
            break; // We have nothing to do...

        case State::Stopping:
        {
            // We shouldn't call this function while stopping!
            Q_ASSERT(false);
            break;
        }
    }
}

void PDFAsynchronousPageCompiler::stop(bool clearCache)
{
    switch (m_state)
    {
        case State::Inactive:
        {
            Q_ASSERT(!m_thread);
            break; // We have nothing to do...
        }

        case State::Active:
        {
            // Stop the engine
            m_state = State::Stopping;

            Q_ASSERT(m_thread);
            {
                // The worker checks the interruption request with the mutex locked
                // just before it waits, so the request must be made under the mutex.
                QMutexLocker locker(&m_mutex);
                m_thread->requestInterruption();
                m_waitCondition.wakeAll();
            }
            m_thread->wait();
            delete m_thread;
            m_thread = nullptr;

            // It is safe to do not use mutex, because
            // we have ended the work thread.
            m_tasks.clear();
            m_activePages.clear();
            m_displayedPages.clear();
            m_isWaitingForDisplayedPages = false;

            if (clearCache)
            {
                // Content of the pages has changed, images derived
                // from the compiled pages are not valid anymore.
                m_cache->clear();
                m_heldDisplayedPages.clear();
                m_compiledPageCosts.clear();
                ++m_cacheGeneration;
            }

            m_state = State::Inactive;
            break;
        }

        case State::Stopping:
        {
            // We shouldn't call this function while stopping!
            Q_ASSERT(false);
            break;
        }
    }
}

void PDFAsynchronousPageCompiler::reset()
{
    stop(true);
    start();
}

bool PDFAsynchronousPageCompiler::hasPendingTasks()
{
    QMutexLocker locker(&m_mutex);
    return !m_tasks.empty();
}

void PDFAsynchronousPageCompiler::setCacheLimit(qsizetype limit)
{
    m_cache->setMaxCost(limit);
}

const PDFPrecompiledPage* PDFAsynchronousPageCompiler::getCompiledPage(PDFInteger pageIndex, CompileMode mode)
{
    if (m_state != State::Active || !m_proxy->getDocument())
    {
        // Engine is not active, always return nullptr
        return nullptr;
    }

    PDFPrecompiledPage* page = findPage(pageIndex).get();

    if (!page && mode != CompileMode::None)
    {
        const bool isCancellable = mode != CompileMode::Persistent;
        const bool isPrefetch = mode == CompileMode::Prefetch;

        QMutexLocker locker(&m_mutex);
        auto it = m_tasks.find(pageIndex);
        if (it == m_tasks.cend())
        {
            // Page, which doesn't fit into the cache together with the displayed
            // pages (for example, a page larger than the whole cache), is not prefetched.
            if (!isPrefetch || !isPrefetchRejected(pageIndex))
            {
                m_tasks.insert(std::make_pair(pageIndex, CompileTask(pageIndex, isCancellable, isPrefetch)));
                m_waitCondition.wakeOne();
            }
        }
        else
        {
            if (!isCancellable && it->second.isCancellable)
            {
                // Jakub Melka: page is already being compiled for the viewport, but now
                // it is requested also by a consumer, which is independent on the viewport.
                // Such task must not be cancelled by scrolling anymore.
                it->second.isCancellable = false;
            }

            if (!isPrefetch)
            {
                // Somebody waits for the page now
                it->second.isPrefetch = false;
            }
        }
    }

    if (page)
    {
        page->markAccessed();
    }

    return page;
}

PDFAsynchronousPageCompiler::PDFPrecompiledPageConstPointer PDFAsynchronousPageCompiler::getCompiledPageShared(PDFInteger pageIndex, CompileMode mode)
{
    if (!getCompiledPage(pageIndex, mode))
    {
        return nullptr;
    }

    return findPage(pageIndex);
}

PDFAsynchronousPageCompiler::PDFPrecompiledPagePointer PDFAsynchronousPageCompiler::findPage(PDFInteger pageIndex) const
{
    if (PDFPrecompiledPagePointer* pagePointer = m_cache->object(pageIndex))
    {
        return *pagePointer;
    }

    auto it = m_heldDisplayedPages.find(pageIndex);
    if (it != m_heldDisplayedPages.cend())
    {
        return it->second;
    }

    return nullptr;
}

qint64 PDFAsynchronousPageCompiler::getHeldPagesCost(PDFInteger excludedPageIndex) const
{
    qint64 cost = 0;

    for (const auto& item : m_heldDisplayedPages)
    {
        if (item.first != excludedPageIndex)
        {
            cost += item.second->getMemoryConsumptionEstimate();
        }
    }

    return cost;
}

bool PDFAsynchronousPageCompiler::isPrefetchRejected(PDFInteger pageIndex) const
{
    auto it = m_compiledPageCosts.find(pageIndex);
    if (it == m_compiledPageCosts.cend())
    {
        // Size of the page is not known yet, it must be compiled first
        return false;
    }

    return it->second + getHeldPagesCost(pageIndex) > m_cache->maxCost();
}

std::vector<PDFAsynchronousPageCompiler::CompileTask> PDFAsynchronousPageCompiler::pickTasksForCompilation()
{
    // Jakub Melka: mutex must be locked by the caller (worker thread).
    std::vector<CompileTask> tasks;

    for (const auto& item : m_tasks)
    {
        if (!item.second.finished && !item.second.isCancelled())
        {
            if (m_isWaitingForDisplayedPages && !std::binary_search(m_displayedPages.cbegin(), m_displayedPages.cend(), item.first))
            {
                // Page view waits for the displayed pages, this page is compiled later
                continue;
            }

            tasks.push_back(item.second);
        }
    }

    // Sort the tasks by priority - pages, which are nearest to the active
    // (visible) pages, are compiled first. Without this, in a large document
    // the user can wait for the currently displayed page, while the engine
    // is busy with pages, which are far away.
    auto getPriority = [this](const CompileTask& task)
    {
        if (m_activePages.empty())
        {
            return PDFInteger(0);
        }

        auto it = std::lower_bound(m_activePages.cbegin(), m_activePages.cend(), task.pageIndex);
        PDFInteger distance = std::numeric_limits<PDFInteger>::max();

        if (it != m_activePages.cend())
        {
            distance = qAbs(*it - task.pageIndex);
        }
        if (it != m_activePages.cbegin())
        {
            distance = qMin(distance, qAbs(*std::prev(it) - task.pageIndex));
        }

        return distance;
    };

    auto comparator = [&getPriority](const CompileTask& left, const CompileTask& right)
    {
        return std::make_pair(getPriority(left), left.pageIndex) < std::make_pair(getPriority(right), right.pageIndex);
    };
    std::sort(tasks.begin(), tasks.end(), comparator);

    if (tasks.size() > MAXIMAL_TASK_BATCH_SIZE)
    {
        tasks.resize(MAXIMAL_TASK_BATCH_SIZE);
    }

    return tasks;
}

void PDFAsynchronousPageCompiler::setDisplayedPages(const std::vector<PDFInteger>& displayedPages, bool isWaiting)
{
    Q_ASSERT(std::is_sorted(displayedPages.cbegin(), displayedPages.cend()));

    {
        QMutexLocker locker(&m_mutex);

        if (m_isWaitingForDisplayedPages != isWaiting || m_displayedPages != displayedPages)
        {
            m_displayedPages = displayedPages;
            m_isWaitingForDisplayedPages = isWaiting;
            m_waitCondition.wakeAll();
        }
    }

    // Hold the compiled displayed pages, release pages, which are not displayed anymore.
    // Released page stays in the cache, if it is still there.
    std::map<PDFInteger, PDFPrecompiledPagePointer> heldDisplayedPages;

    if (m_state == State::Active)
    {
        for (PDFInteger pageIndex : displayedPages)
        {
            if (PDFPrecompiledPagePointer page = findPage(pageIndex))
            {
                heldDisplayedPages[pageIndex] = std::move(page);
            }
        }
    }

    m_heldDisplayedPages = std::move(heldDisplayedPages);
}

void PDFAsynchronousPageCompiler::setActivePages(const std::vector<PDFInteger>& activePages)
{
    if (m_state != State::Active)
    {
        return;
    }

    Q_ASSERT(std::is_sorted(activePages.cbegin(), activePages.cend()));

    QMutexLocker locker(&m_mutex);
    m_activePages = activePages;

    // Cancel and erase all tasks of pages, which are not active anymore. Already
    // running compilation is stopped via the cancellation flag, the result of such
    // compilation is thrown away by the worker thread.
    for (auto it = m_tasks.begin(); it != m_tasks.end();)
    {
        const CompileTask& task = it->second;

        if (task.finished || !task.isCancellable ||
            std::binary_search(activePages.cbegin(), activePages.cend(), task.pageIndex))
        {
            ++it;
            continue;
        }

        task.cancel();
        it = m_tasks.erase(it);
    }
}

void PDFAsynchronousPageCompiler::smartClearCache(const int milisecondsLimit, const std::vector<PDFInteger>& activePages)
{
    if (m_state != State::Active)
    {
        // Jakub Melka: Cache clearing can be done only in active state
        return;
    }

    QMutexLocker locker(&m_mutex);

    Q_ASSERT(std::is_sorted(activePages.cbegin(), activePages.cend()));

    QList<PDFInteger> pageIndices = m_cache->keys();
    for (const PDFInteger pageIndex : pageIndices)
    {
        if (std::binary_search(activePages.cbegin(), activePages.cend(), pageIndex))
        {
            // We do not remove active page
            continue;
        }

        const PDFPrecompiledPagePointer* pagePointer = m_cache->object(pageIndex);
        if (pagePointer && (*pagePointer)->hasExpired(milisecondsLimit))
        {
            m_cache->remove(pageIndex);
        }
    }
}

void PDFAsynchronousPageCompiler::onPageCompiled()
{
    std::vector<PDFInteger> compiledPages;
    std::map<PDFInteger, PDFRenderError> errors;

    {
        QMutexLocker locker(&m_mutex);

        // Search all tasks for finished tasks
        for (auto it = m_tasks.begin(); it != m_tasks.end();)
        {
            CompileTask& task = it->second;
            if (task.finished)
            {
                if (m_state == State::Active)
                {
                    // If we are in active state, try to store precompiled page
                    auto compiledPage = std::make_shared<PDFPrecompiledPage>(std::move(task.precompiledPage));
                    compiledPage->markAccessed();
                    qint64 memoryConsumptionEstimate = compiledPage->getMemoryConsumptionEstimate();
                    m_compiledPageCosts[it->first] = memoryConsumptionEstimate;

                    const bool isDisplayed = std::binary_search(m_displayedPages.cbegin(), m_displayedPages.cend(), it->first);
                    if (task.isPrefetch && !isDisplayed && isPrefetchRejected(it->first))
                    {
                        // Nobody waits for this page and it doesn't fit into the cache together with
                        // the displayed pages (it would remove them from the cache). It is thrown
                        // away, it will be compiled again, when it is displayed.
                        it = m_tasks.erase(it);
                        continue;
                    }

                    // Page larger than the whole cache must be drawn too (a large plan sheet can
                    // have hundreds of megabytes). It is held in the cache alone - its cost is
                    // the size of the cache, so all other pages are removed.
                    const qint64 cost = qMin<qint64>(memoryConsumptionEstimate, m_cache->maxCost());

                    if (isDisplayed)
                    {
                        // Displayed page is held, so it is never removed by the other pages
                        m_heldDisplayedPages[it->first] = compiledPage;
                    }

                    PDFPrecompiledPagePointer* page = new PDFPrecompiledPagePointer(std::move(compiledPage));
                    if (m_cache->insert(it->first, page, cost))
                    {
                        compiledPages.push_back(it->first);
                    }
                    else
                    {
                        // We can't insert page to the cache, because cache size is too small. We will
                        // emit error string to inform the user, that cache is too small.
                        QString message = PDFTranslationContext::tr("Precompiled page size is too high (%1 kB). Cache size is %2 kB. Increase the cache size!")
                                              .arg(qint64(memoryConsumptionEstimate / 1024))
                                              .arg(qint64(m_cache->maxCost() / 1024));
                        errors[it->first] = PDFRenderError(RenderErrorType::Error, message);
                    }
                }

                it = m_tasks.erase(it);
            }
            else
            {
                // Just increment the counter
                ++it;
            }
        }
    }

    for (const auto& error : errors)
    {
        Q_EMIT renderingError(error.first, { error.second });
    }

    if (!compiledPages.empty())
    {
        Q_ASSERT(std::is_sorted(compiledPages.cbegin(), compiledPages.cend()));
        Q_EMIT pageImageChanged(false, compiledPages);
    }
}

PDFAsynchronousTextLayoutCompiler::PDFAsynchronousTextLayoutCompiler(PDFDrawWidgetProxy* proxy) :
    BaseClass(proxy),
    m_proxy(proxy),
    m_isRunning(false),
    m_cache(std::bind(&PDFAsynchronousTextLayoutCompiler::createTextLayout, this, std::placeholders::_1))
{
    connect(&m_textLayoutCompileFutureWatcher, &QFutureWatcher<PDFTextLayoutStorage>::finished, this, &PDFAsynchronousTextLayoutCompiler::onTextLayoutCreated);
}

void PDFAsynchronousTextLayoutCompiler::start()
{
    switch (m_state)
    {
        case State::Inactive:
        {
            m_state = State::Active;
            break;
        }

        case State::Active:
            break; // We have nothing to do...

        case State::Stopping:
        {
            // We shouldn't call this function while stopping!
            Q_ASSERT(false);
            break;
        }
    }
}

void PDFAsynchronousTextLayoutCompiler::stop(bool clearCache)
{
    switch (m_state)
    {
        case State::Inactive:
            break; // We have nothing to do...

        case State::Active:
        {
            // Stop the engine
            m_state = State::Stopping;
            m_textLayoutCompileFutureWatcher.waitForFinished();

            if (clearCache)
            {
                m_textLayouts = std::nullopt;
                m_cache.clear();
            }

            m_state = State::Inactive;
            break;
        }

        case State::Stopping:
        {
            // We shouldn't call this function while stopping!
            Q_ASSERT(false);
            break;
        }
    }
}

void PDFAsynchronousTextLayoutCompiler::reset()
{
    stop(true);
    start();
}

PDFTextLayout PDFAsynchronousTextLayoutCompiler::createTextLayout(PDFInteger pageIndex)
{
    PDFTextLayout result;

    if (isTextLayoutReady())
    {
        result = getTextLayout(pageIndex);
    }
    else
    {
        if (m_state != State::Active || !m_proxy->getDocument())
        {
            // Engine is not active, do not calculate layout
            return result;
        }

        const PDFCatalog* catalog = m_proxy->getDocument()->getCatalog();
        if (pageIndex < 0 || pageIndex >= PDFInteger(catalog->getPageCount()))
        {
            return result;
        }

        if (!catalog->getPage(pageIndex))
        {
            // Invalid page index
            return result;
        }

        const PDFPage* page = catalog->getPage(pageIndex);
        Q_ASSERT(page);

        bool guard = false;
        m_proxy->getFontCache()->setCacheShrinkEnabled(&guard, false);

        PDFCMSPointer cms = m_proxy->getCMSManager()->getCurrentCMS();
        PDFTextLayoutGenerator generator(m_proxy->getFeatures(), page, m_proxy->getDocument(), m_proxy->getFontCache(), m_proxy->getIccProfileCache(), cms.data(), m_proxy->getOptionalContentActivity(), QTransform(), m_proxy->getMeshQualitySettings());
        generator.processContents();
        result = generator.createTextLayout();
        m_proxy->getFontCache()->setCacheShrinkEnabled(&guard, true);
    }

    return result;
}

PDFTextLayout PDFAsynchronousTextLayoutCompiler::getTextLayout(PDFInteger pageIndex)
{
    if (m_state != State::Active || !m_proxy->getDocument())
    {
        // Engine is not active, always return empty layout
        return PDFTextLayout();
    }

    if (m_textLayouts)
    {
        return m_textLayouts->getTextLayout(pageIndex);
    }

    return PDFTextLayout();
}

PDFTextLayoutGetter PDFAsynchronousTextLayoutCompiler::getTextLayoutLazy(PDFInteger pageIndex)
{
    return PDFTextLayoutGetter(&m_cache, pageIndex);
}

PDFTextSelection PDFAsynchronousTextLayoutCompiler::getTextSelectionAll(QColor color) const
{
    PDFTextSelection result;

    if (m_textLayouts)
    {
        const PDFTextLayoutStorage& textLayouts = *m_textLayouts;

        QMutex mutex;
        PDFIntegerRange<size_t> pageRange(0, textLayouts.getCount());
        auto selectPageText = [&mutex, &textLayouts, &result, color](PDFInteger pageIndex)
        {
            PDFTextLayout textLayout = textLayouts.getTextLayout(pageIndex);
            PDFTextSelectionItems items;

            const PDFTextBlocks& blocks = textLayout.getTextBlocks();
            for (size_t blockId = 0, blockCount = blocks.size(); blockId < blockCount; ++blockId)
            {
                const PDFTextBlock& block = blocks[blockId];
                const PDFTextLines& lines = block.getLines();

                if (!lines.empty())
                {
                    const PDFTextLine& lastLine = lines.back();
                    Q_ASSERT(!lastLine.getCharacters().empty());

                    PDFCharacterPointer ptrStart;
                    ptrStart.pageIndex = pageIndex;
                    ptrStart.blockIndex = blockId;
                    ptrStart.lineIndex = 0;
                    ptrStart.characterIndex = 0;

                    PDFCharacterPointer ptrEnd;
                    ptrEnd.pageIndex = pageIndex;
                    ptrEnd.blockIndex = blockId;
                    ptrEnd.lineIndex = lines.size() - 1;
                    ptrEnd.characterIndex = lastLine.getCharacters().size() - 1;

                    items.emplace_back(ptrStart, ptrEnd);
                }
            }

            QMutexLocker lock(&mutex);
            result.addItems(qMove(items), color);
        };
        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, pageRange.begin(), pageRange.end(), selectPageText);
    }

    result.build();
    return result;
}

void PDFAsynchronousTextLayoutCompiler::makeTextLayout()
{
    if (m_state != State::Active || !m_proxy->getDocument())
    {
        // Engine is not active, do not calculate layout
        return;
    }

    if (m_textLayouts.has_value())
    {
        // Value is computed already
        return;
    }

    if (m_isRunning)
    {
        // Text layout is already being processed
        return;
    }

    // Jakub Melka: Mark, that we are running (test for future is not enough,
    // because future can finish before this function exits, for example)
    m_isRunning = true;

    ProgressStartupInfo info;
    info.showDialog = false;
    info.text = tr("Indexing document contents...");

    m_proxy->getFontCache()->setCacheShrinkEnabled(this, false);
    const PDFCatalog* catalog = m_proxy->getDocument()->getCatalog();
    m_proxy->getProgress()->start(catalog->getPageCount(), qMove(info));

    PDFCMSPointer cms = m_proxy->getCMSManager()->getCurrentCMS();

    auto createTextLayout = [this, cms, catalog]() -> PDFTextLayoutStorage
    {
        PDFTextLayoutStorage result(catalog->getPageCount());
        auto generateTextLayout = [this, &result, cms, catalog](PDFInteger pageIndex)
        {
            if (!catalog->getPage(pageIndex))
            {
                // Invalid page index
                result.setTextLayout(pageIndex, PDFTextLayout());
                return;
            }

            const PDFPage* page = catalog->getPage(pageIndex);
            Q_ASSERT(page);

            PDFTextLayoutGenerator generator(m_proxy->getFeatures(), page, m_proxy->getDocument(), m_proxy->getFontCache(), m_proxy->getIccProfileCache(), cms.data(), m_proxy->getOptionalContentActivity(), QTransform(), m_proxy->getMeshQualitySettings());
            generator.processContents();
            result.setTextLayout(pageIndex, generator.createTextLayout());
            m_proxy->getProgress()->step();
        };

        auto pageRange = PDFIntegerRange<PDFInteger>(0, catalog->getPageCount());
        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, pageRange.begin(), pageRange.end(), generateTextLayout);
        return result;
    };

    Q_ASSERT(!m_textLayoutCompileFuture.isRunning());
    m_textLayoutCompileFuture = QtConcurrent::run(createTextLayout);
    m_textLayoutCompileFutureWatcher.setFuture(m_textLayoutCompileFuture);
}

void PDFAsynchronousTextLayoutCompiler::onTextLayoutCreated()
{
    m_proxy->getFontCache()->setCacheShrinkEnabled(this, true);
    m_proxy->getProgress()->finish();
    m_cache.clear();

    m_textLayouts = m_textLayoutCompileFuture.result();
    m_isRunning = false;
    Q_EMIT textLayoutChanged();
}

}   // namespace pdf
