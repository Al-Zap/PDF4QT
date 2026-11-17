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

#include "pdfcms.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfform.h"
#include "pdfprogress.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgetformmanager.h"
#include "pdfwidgettool.h"
#include "pdfadvancedtools.h"
#include "pdfrecoverymanager.h"

#include <QtTest>
#include <QAction>
#include <QBuffer>
#include <QPainter>
#include <QPlainTextEdit>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

using namespace pdf;

/// Tests of the form fields, which are filled by the user in the widget - the document
/// is changed by the mouse and the result is checked in the document and in the drawn page.
/// Fields with a text are checked by the drawn page only.
class FormFieldsTest : public QObject
{
    Q_OBJECT

private slots:
    void checkBoxWithAppearance_data();
    void checkBoxWithAppearance();
    void checkBoxWithoutCheckedAppearance_data();
    void checkBoxWithoutCheckedAppearance();
    void checkBoxCheckedStateFromValue_data();
    void checkBoxCheckedStateFromValue();
    void checkBoxWithoutAppearanceState();
    void checkBoxCheckedStateIsKept_data();
    void checkBoxCheckedStateIsKept();
    void checkBoxUncheckedAppearanceIsKept_data();
    void checkBoxUncheckedAppearanceIsKept();
    void radioButtons();
    void savedAndReopened();
    void textWithoutAppearance_data();
    void textWithoutAppearance();
    void choiceDefaultAppearance();
    void inheritedDefaultAppearance_data();
    void inheritedDefaultAppearance();
    void tabOrder_data();
    void tabOrder();
    void tabSkipsReadOnlyFields();
    void tabSkipsHiddenFields_data();
    void tabSkipsHiddenFields();
    void flattenText_data();
    void flattenText();
    void flattenCheckBox();
    void flattenKeepsOtherAnnotations();
    void flattenRemovesHiddenValuesAndScripts();
    void autocomplete();
    void commitPendingChanges();
    void typewriter();
    void recoveryCopy();
    void recoveryCopyLockedByOtherWindow();
    void recoveryCopyOfferedAndCleanup();
    void comboBoxKeyboard_data();
    void comboBoxKeyboard();
    void fillRealForm();
};

/// Creates the documents with the form fields
class FormFieldsTestHelper
{
public:
    FormFieldsTestHelper() = delete;

    /// Appearance streams of the check box widget
    enum class Appearance
    {
        Complete,       ///< Normal appearance of both states
        CheckedOnly,    ///< Normal appearance of the checked state, unchecked state has only a down appearance
        None,           ///< No appearance dictionary
        UncheckedOnly,  ///< Normal appearance of the unchecked state only
        UncheckedDown,  ///< Normal appearance of the unchecked state only, both states have a down appearance
        UncheckedGreen, ///< Normal appearance of the unchecked state only, it is a green rectangle
        SingleStream    ///< Normal appearance is a single stream instead of the dictionary of the states
    };

    static constexpr QSizeF PAGE_SIZE = QSizeF(200, 200);
    static constexpr QRectF CHECK_BOX = QRectF(90, 90, 20, 20);
    static constexpr QRectF RADIO_BUTTON_A = QRectF(40, 90, 20, 20);
    static constexpr QRectF RADIO_BUTTON_B = QRectF(140, 90, 20, 20);
    static constexpr QRectF TEXT_FIELD = QRectF(40, 80, 120, 40);

    /// Field with a text, which is drawn from the value of the field
    enum class TextField
    {
        TextBox,
        ComboBox,
        ListBox
    };

    /// Appearance stream of the document itself is a green rectangle (checked), or it is empty (unchecked)
    static PDFObjectReference createAppearanceStream(PDFDocumentBuilder& builder, QRectF rectangle, bool checked)
    {
        PDFContentStreamBuilder content(PAGE_SIZE, PDFContentStreamBuilder::CoordinateSystem::PDF);
        QPainter* painter = content.begin();
        if (checked)
        {
            painter->fillRect(rectangle, Qt::green);
        }
        const PDFContentStreamBuilder::ContentStream stream = content.end(painter);
        const std::vector<PDFObject> objects = builder.copyFrom({ stream.resources, stream.contents }, stream.document.getStorage(), true);

        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Type");
        factory << WrapName("XObject");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Subtype");
        factory << WrapName("Form");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("BBox");
        factory << rectangle;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Resources");
        factory << objects[0];
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(objects[1].getReference(), factory.takeObject());
        return objects[1].getReference();
    }

    static void addName(PDFObjectFactory& factory, const char* key, const QByteArray& name)
    {
        if (!name.isEmpty())
        {
            factory.beginDictionaryItem(key);
            factory << WrapName(name);
            factory.endDictionaryItem();
        }
    }

    static void addStates(PDFObjectFactory& factory, const char* key, const std::vector<std::pair<QByteArray, PDFObjectReference>>& states)
    {
        factory.beginDictionaryItem(key);
        factory.beginDictionary();
        for (const auto& state : states)
        {
            factory.beginDictionaryItem(state.first);
            factory << state.second;
            factory.endDictionaryItem();
        }
        factory.endDictionary();
        factory.endDictionaryItem();
    }

    static void beginWidget(PDFObjectFactory& factory, PDFObjectReference page, QRectF rectangle)
    {
        factory.beginDictionary();
        factory.beginDictionaryItem("Type");
        factory << WrapName("Annot");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Subtype");
        factory << WrapName("Widget");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Rect");
        factory << rectangle;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("P");
        factory << page;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("F");
        factory << 4;
        factory.endDictionaryItem();
    }

    static void beginField(PDFObjectFactory& factory, const QString& name, PDFInteger flags)
    {
        factory.beginDictionaryItem("FT");
        factory << WrapName("Btn");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("T");
        factory << name;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Ff");
        factory << flags;
        factory.endDictionaryItem();
    }

    static void addToPage(PDFDocumentBuilder& builder, PDFObjectReference page, const std::vector<PDFObjectReference>& widgets)
    {
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Annots");
        factory << widgets;
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.appendTo(page, factory.takeObject());
    }

    /// Creates a document with a single check box
    /// \param appearance Appearance streams of the widget
    /// \param onState Name of the checked state in the appearance dictionary
    /// \param value Value of the field (/V), nothing if empty
    /// \param defaultValue Default value of the field (/DV), nothing if empty
    /// \param appearanceState Appearance state of the widget (/AS), nothing if empty
    static PDFDocument createCheckBoxDocument(Appearance appearance,
                                              const QByteArray& onState,
                                              const QByteArray& value,
                                              const QByteArray& defaultValue,
                                              const QByteArray& appearanceState)
    {
        PDFDocumentBuilder builder;
        const PDFObjectReference page = builder.appendPage(QRectF(QPointF(0, 0), PAGE_SIZE));

        PDFObjectFactory factory;
        beginWidget(factory, page, CHECK_BOX);
        beginField(factory, "Check", 0);
        addName(factory, "V", value);
        addName(factory, "DV", defaultValue);
        addName(factory, "AS", appearanceState);

        if (appearance != Appearance::None)
        {
            const PDFObjectReference checked = createAppearanceStream(builder, CHECK_BOX, true);
            const PDFObjectReference unchecked = createAppearanceStream(builder, CHECK_BOX, false);

            factory.beginDictionaryItem("AP");
            factory.beginDictionary();
            switch (appearance)
            {
                case Appearance::Complete:
                    addStates(factory, "N", { { "Off", unchecked }, { onState, checked } });
                    break;

                case Appearance::CheckedOnly:
                    addStates(factory, "N", { { onState, checked } });
                    addStates(factory, "D", { { "Off", unchecked }, { onState, checked } });
                    break;

                case Appearance::UncheckedOnly:
                    addStates(factory, "N", { { "Off", unchecked } });
                    break;

                case Appearance::UncheckedDown:
                    addStates(factory, "N", { { "Off", unchecked } });
                    addStates(factory, "D", { { "Off", unchecked }, { onState, checked } });
                    break;

                case Appearance::UncheckedGreen:
                    addStates(factory, "N", { { "Off", checked } });
                    break;

                case Appearance::SingleStream:
                    factory.beginDictionaryItem("N");
                    factory << unchecked;
                    factory.endDictionaryItem();
                    break;

                default:
                    Q_ASSERT(false);
                    break;
            }
            factory.endDictionary();
            factory.endDictionaryItem();
        }

        factory.endDictionary();
        const PDFObjectReference widget = builder.addObject(factory.takeObject());
        addToPage(builder, page, { widget });
        builder.createAcroForm({ widget });
        return builder.build();
    }

    /// Creates a document with a group of two radio buttons with the states A and B,
    /// which can't be all turned off, the button A is selected.
    static PDFDocument createRadioButtonDocument()
    {
        PDFDocumentBuilder builder;
        const PDFObjectReference page = builder.appendPage(QRectF(QPointF(0, 0), PAGE_SIZE));

        std::vector<PDFObjectReference> widgets;
        for (const auto& [state, rectangle] : { std::pair<QByteArray, QRectF>{ "A", RADIO_BUTTON_A }, std::pair<QByteArray, QRectF>{ "B", RADIO_BUTTON_B } })
        {
            PDFObjectFactory factory;
            beginWidget(factory, page, rectangle);
            addName(factory, "AS", state == "A" ? state : QByteArray("Off"));
            factory.beginDictionaryItem("AP");
            factory.beginDictionary();
            addStates(factory, "N", { { "Off", createAppearanceStream(builder, rectangle, false) }, { state, createAppearanceStream(builder, rectangle, true) } });
            factory.endDictionary();
            factory.endDictionaryItem();
            factory.endDictionary();
            widgets.push_back(builder.addObject(factory.takeObject()));
        }

        PDFObjectFactory factory;
        factory.beginDictionary();
        beginField(factory, "Radio", PDFFormField::Radio | PDFFormField::NoToggleToOff);
        addName(factory, "V", "A");
        factory.beginDictionaryItem("Kids");
        factory << widgets;
        factory.endDictionaryItem();
        factory.endDictionary();
        const PDFObjectReference field = builder.addObject(factory.takeObject());

        for (const PDFObjectReference& widget : widgets)
        {
            factory.beginDictionary();
            factory.beginDictionaryItem("Parent");
            factory << field;
            factory.endDictionaryItem();
            factory.endDictionary();
            builder.mergeTo(widget, factory.takeObject());
        }

        addToPage(builder, page, widgets);
        builder.createAcroForm({ field });
        return builder.build();
    }

    /// Creates a document with a single field with the text 'Hello'. The field
    /// has its own default appearance, which differs from the one of the form.
    /// Choice fields have the options 'Hello' and 'World', the second option
    /// is selected in the list box, so the first one is drawn as a plain text.
    /// \param textField Type of the field
    /// \param content Content of the appearance stream of the widget
    /// \param needAppearances Viewer is asked to construct the appearance streams
    /// \param defaultAppearance Default appearance of the field itself, nothing if empty
    /// \param parentCount Number of the parent fields above the field. The type of the field
    ///        and the default appearance '0 g /Helv 20 Tf' are defined only by the root field.
    static PDFDocument createTextFieldDocument(TextField textField,
                                               const QByteArray& content,
                                               bool needAppearances,
                                               const QByteArray& defaultAppearance = "0 g /Helv 20 Tf",
                                               int parentCount = 0)
    {
        PDFDocumentBuilder builder;
        const PDFObjectReference page = builder.appendPage(QRectF(QPointF(0, 0), PAGE_SIZE));

        PDFDictionaryBuilder font;
        font.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Font"));
        font.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Type1"));
        font.addEntry(PDFInplaceOrMemoryString("BaseFont"), PDFObject::createName("Helvetica"));
        PDFDictionaryBuilder fonts;
        fonts.addEntry(PDFInplaceOrMemoryString("Helv"), PDFObject::createReference(builder.addObject(PDFObject::createDictionary(std::move(font)))));
        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("Font"), PDFObject::createDictionary(std::move(fonts)));
        const PDFObject resourcesObject = PDFObject::createDictionary(std::move(resources));

        PDFObjectFactory streamFactory;
        streamFactory.beginDictionary();
        streamFactory.beginDictionaryItem("Type");
        streamFactory << WrapName("XObject");
        streamFactory.endDictionaryItem();
        streamFactory.beginDictionaryItem("Subtype");
        streamFactory << WrapName("Form");
        streamFactory.endDictionaryItem();
        streamFactory.beginDictionaryItem("BBox");
        streamFactory << QRectF(QPointF(0, 0), TEXT_FIELD.size());
        streamFactory.endDictionaryItem();
        streamFactory.beginDictionaryItem("Resources");
        streamFactory << resourcesObject;
        streamFactory.endDictionaryItem();
        streamFactory.beginDictionaryItem("Length");
        streamFactory << PDFInteger(content.size());
        streamFactory.endDictionaryItem();
        streamFactory.endDictionary();
        const PDFObject streamDictionary = streamFactory.takeObject();
        const PDFObjectReference stream = builder.addObject(PDFObject::createStream(PDFStream(PDFDictionaryBuilder(*streamDictionary.getDictionary()), QByteArray(content))));

        PDFObjectFactory factory;
        beginWidget(factory, page, TEXT_FIELD);
        if (parentCount == 0)
        {
            factory.beginDictionaryItem("FT");
            factory << WrapName(textField == TextField::TextBox ? "Tx" : "Ch");
            factory.endDictionaryItem();
        }
        factory.beginDictionaryItem("T");
        factory << QString("Text");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Ff");
        factory << PDFInteger(textField == TextField::ComboBox ? PDFFormField::Combo : PDFFormField::None);
        factory.endDictionaryItem();
        factory.beginDictionaryItem("V");
        factory << QString(textField == TextField::ListBox ? "World" : "Hello");
        factory.endDictionaryItem();
        if (!defaultAppearance.isEmpty())
        {
            factory.beginDictionaryItem("DA");
            factory << WrapString(defaultAppearance);
            factory.endDictionaryItem();
        }
        if (textField != TextField::TextBox)
        {
            factory.beginDictionaryItem("Opt");
            factory.beginArray();
            factory << QString("Hello");
            factory << QString("World");
            factory.endArray();
            factory.endDictionaryItem();
        }
        factory.beginDictionaryItem("AP");
        factory.beginDictionary();
        factory.beginDictionaryItem("N");
        factory << stream;
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endDictionaryItem();
        factory.endDictionary();
        const PDFObjectReference widget = builder.addObject(factory.takeObject());
        addToPage(builder, page, { widget });

        PDFObjectReference field = widget;
        for (int i = 0; i < parentCount; ++i)
        {
            factory.beginDictionary();
            factory.beginDictionaryItem("T");
            factory << QString("Parent%1").arg(i);
            factory.endDictionaryItem();
            factory.beginDictionaryItem("Kids");
            factory << std::vector<PDFObjectReference>{ field };
            factory.endDictionaryItem();
            if (i + 1 == parentCount)
            {
                factory.beginDictionaryItem("FT");
                factory << WrapName(textField == TextField::TextBox ? "Tx" : "Ch");
                factory.endDictionaryItem();
                factory.beginDictionaryItem("DA");
                factory << WrapString("0 g /Helv 20 Tf");
                factory.endDictionaryItem();
            }
            factory.endDictionary();
            const PDFObjectReference parent = builder.addObject(factory.takeObject());

            factory.beginDictionary();
            factory.beginDictionaryItem("Parent");
            factory << parent;
            factory.endDictionaryItem();
            factory.endDictionary();
            builder.mergeTo(field, factory.takeObject());
            field = parent;
        }

        factory.beginDictionary();
        factory.beginDictionaryItem("NeedAppearances");
        factory << needAppearances;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("DR");
        factory << resourcesObject;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("DA");
        factory << WrapString("0 g /Helv 6 Tf");
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(builder.createAcroForm({ field }), factory.takeObject());
        return builder.build();
    }

    /// Creates a document with text fields for the tests of the tab order. The first page has
    /// fields A (top right), B (top left) and C (bottom left), the annotation array of the page
    /// is C, A, B. The second page has the field D. The fields of the form are in the reverse
    /// order D, C, B, A.
    /// \param tabs Tab order of the first page (entry Tabs), nothing if empty
    /// \param readOnlyField Name of the read only field, nothing if empty
    static PDFDocument createTabOrderDocument(const QByteArray& tabs,
                                              const QString& readOnlyField = QString(),
                                              const QString& hiddenField = QString(),
                                              PDFInteger hiddenFlags = 0,
                                              const QString& hiddenValue = QString())
    {
        PDFDocumentBuilder builder;
        const PDFObjectReference page1 = builder.appendPage(QRectF(QPointF(0, 0), PAGE_SIZE));
        const PDFObjectReference page2 = builder.appendPage(QRectF(QPointF(0, 0), PAGE_SIZE));

        auto createField = [&](PDFObjectReference page, const QString& name, QRectF rectangle)
        {
            PDFObjectFactory factory;
            beginWidget(factory, page, rectangle);
            factory.beginDictionaryItem("FT");
            factory << WrapName("Tx");
            factory.endDictionaryItem();
            factory.beginDictionaryItem("T");
            factory << name;
            factory.endDictionaryItem();
            factory.beginDictionaryItem("Ff");
            factory << PDFInteger(name == readOnlyField ? PDFFormField::ReadOnly : PDFFormField::None);
            factory.endDictionaryItem();
            factory.beginDictionaryItem("DA");
            factory << WrapString("0 g /Helv 12 Tf");
            factory.endDictionaryItem();
            if (name == hiddenField)
            {
                factory.beginDictionaryItem("F");
                factory << hiddenFlags;
                factory.endDictionaryItem();
                if (!hiddenValue.isEmpty())
                {
                    factory.beginDictionaryItem("V");
                    factory << hiddenValue;
                    factory.endDictionaryItem();
                }
            }
            factory.endDictionary();
            return builder.addObject(factory.takeObject());
        };

        const PDFObjectReference a = createField(page1, "A", QRectF(120, 150, 60, 20));
        const PDFObjectReference b = createField(page1, "B", QRectF(20, 152, 60, 20));
        const PDFObjectReference c = createField(page1, "C", QRectF(20, 50, 60, 20));
        const PDFObjectReference d = createField(page2, "D", QRectF(20, 150, 60, 20));
        addToPage(builder, page1, { c, a, b });
        addToPage(builder, page2, { d });

        if (!tabs.isEmpty())
        {
            PDFObjectFactory factory;
            factory.beginDictionary();
            addName(factory, "Tabs", tabs);
            factory.endDictionary();
            builder.mergeTo(page1, factory.takeObject());
        }

        builder.createAcroForm({ d, c, b, a });
        return builder.build();
    }

    /// Saves the document to the memory and reads it again
    /// \param document Document
    /// \param[out] ok Document has been written and read
    static PDFDocument reopenDocument(const PDFDocument* document, bool& ok)
    {
        QByteArray data;
        QBuffer buffer(&data);
        ok = buffer.open(QIODevice::WriteOnly);

        PDFDocumentWriter writer(nullptr);
        ok = ok && bool(writer.write(&buffer, document));
        buffer.close();

        PDFDocumentReader reader(nullptr, nullptr, false, false);
        PDFDocument result = reader.readFromBuffer(data);
        ok = ok && reader.getReadingResult() == PDFDocumentReader::Result::OK;
        return result;
    }
};

/// The widget, the annotation manager, the form manager and the tools are connected
/// the same way, as the application connects them - a modified document is set to all
/// components. Mouse events are sent to the widget, so they are dispatched to the form
/// manager by the same way, as in the application.
struct FormFixture
{
    PDFDocumentPointer document;
    PDFCMSManager cms{ nullptr };
    PDFProgress progress{ nullptr };
    PDFWidget widget{ &cms, RendererEngine::QPainter, nullptr };
    PDFWidgetAnnotationManager annotations{ widget.getDrawWidgetProxy(), nullptr };
    PDFWidgetFormManager forms{ widget.getDrawWidgetProxy(), nullptr };
    std::array<QAction, 10> actions;
    PDFToolManager tools{ widget.getDrawWidgetProxy(),
        { &actions[0], &actions[1], &actions[2], &actions[3], &actions[4], &actions[5],
          &actions[6], &actions[7], &actions[8], &actions[9] }, nullptr, &widget };
    int modificationCount = 0;

    explicit FormFixture(PDFDocument initialDocument) :
        document(new PDFDocument(std::move(initialDocument)))
    {
        widget.resize(800, 800);
        widget.getDrawWidget()->getWidget()->resize(800, 800);
        widget.setAnnotationManager(&annotations);
        widget.setFormManager(&forms);
        widget.setToolManager(&tools);
        widget.getDrawWidgetProxy()->setProgress(&progress);
        forms.setAnnotationManager(&annotations);
        annotations.setFormManager(&forms);

        // Form fields are not highlighted, so the colors of the drawn page are not affected
        forms.setAppearanceFlags(PDFFormManager::FormAppearanceFlags());
        apply(PDFModifiedDocument(document, nullptr));

        QObject::connect(&forms, &PDFFormManager::documentModified, &forms, [this](PDFModifiedDocument modified)
        {
            ++modificationCount;
            PDFDocumentPointer oldDocument = document;
            document = modified;
            apply(PDFModifiedDocument(document, nullptr, modified.getFlags()));
        });
    }

    ~FormFixture()
    {
        tools.setDocument(PDFModifiedDocument());
        forms.setDocument(PDFModifiedDocument());
        annotations.setDocument(PDFModifiedDocument());
        widget.setDocument(PDFModifiedDocument(), {});
        widget.setFormManager(nullptr);
        widget.setAnnotationManager(nullptr);
        widget.setToolManager(nullptr);
    }

    void apply(PDFModifiedDocument modified)
    {
        widget.setDocument(modified, {});
        annotations.setDocument(modified);
        forms.setDocument(modified);
        tools.setDocument(modified);
    }

    /// Returns widget annotation of the first page
    PDFObjectReference widgetReference(size_t index = 0) const
    {
        return document->getCatalog()->getPage(0)->getAnnotations().at(index);
    }

    const PDFDictionary* dictionary(PDFObjectReference reference) const
    {
        return document->getDictionaryFromObject(document->getObjectByReference(reference));
    }

    /// Returns true, if dictionary has a name with the given value. Empty name is
    /// distinguished from the missing one - empty value means that there is no entry.
    bool hasName(PDFObjectReference reference, const char* key, const QByteArray& name) const
    {
        const PDFObject& object = document->getObject(dictionary(reference)->get(key));
        return name.isEmpty() ? object.isNull() : (object.isName() && object.getString() == name);
    }

    PDFObject appearance(PDFObjectReference reference) const
    {
        return dictionary(reference)->get("AP");
    }

    PDFObject downAppearance(PDFObjectReference reference) const
    {
        const PDFDictionary* appearanceDictionary = document->getDictionaryFromObject(appearance(reference));
        return appearanceDictionary ? appearanceDictionary->get("D") : PDFObject();
    }

    /// Returns the states, which have a stream in the normal appearance
    /// dictionary. Single stream is returned as the state with empty name.
    QByteArrayList normalStates(PDFObjectReference reference) const
    {
        QByteArrayList states;
        if (const PDFDictionary* appearanceDictionary = document->getDictionaryFromObject(appearance(reference)))
        {
            const PDFObject& normal = document->getObject(appearanceDictionary->get("N"));
            if (normal.isStream())
            {
                states << QByteArray();
            }
            else if (normal.isDictionary())
            {
                for (size_t i = 0; i < normal.getDictionary()->getCount(); ++i)
                {
                    if (document->getObject(normal.getDictionary()->getValue(i)).isStream())
                    {
                        states << normal.getDictionary()->getKey(i).getString();
                    }
                }
            }
        }

        std::sort(states.begin(), states.end());
        return states;
    }

    /// Returns the appearance stream of the state from the normal appearance dictionary
    PDFObject normalAppearance(PDFObjectReference reference, const QByteArray& state) const
    {
        const PDFDictionary* appearanceDictionary = document->getDictionaryFromObject(appearance(reference));
        const PDFDictionary* normal = appearanceDictionary ? document->getDictionaryFromObject(appearanceDictionary->get("N")) : nullptr;
        return normal ? normal->get(state) : PDFObject();
    }

    QRectF rectangle(PDFObjectReference reference) const
    {
        PDFDocumentDataLoaderDecorator loader(document.data());
        return loader.readRectangle(dictionary(reference)->get("Rect"), QRectF());
    }

    QPoint device(const QPointF& pagePoint) const
    {
        const PDFWidgetSnapshot snapshot = widget.getDrawWidgetProxy()->getSnapshot();
        return snapshot.items.empty() ? QPoint() : snapshot.items.front().pageToDeviceMatrix.map(pagePoint).toPoint();
    }

    /// Clicks by the left mouse button to the page point
    void click(const QPointF& pagePoint)
    {
        QWidget* target = widget.getDrawWidget()->getWidget();
        const QPoint position = device(pagePoint);

        QMouseEvent press(QEvent::MouseButtonPress, position, target->mapToGlobal(position), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, position, target->mapToGlobal(position), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &release);
    }

    /// Returns color of the drawn widget at the page point
    QColor color(const QPointF& pagePoint)
    {
        const QImage image = widget.getDrawWidget()->getWidget()->grab().toImage();
        return image.pixelColor(device(pagePoint) * image.devicePixelRatio());
    }

    /// Check box drawn by the library has a cross, which goes through its center,
    /// so the center is darker, than the point between the arms of the cross.
    bool isMarked(const QRectF& checkBox)
    {
        const QPointF center = checkBox.center();
        const QPointF reference = center + QPointF(checkBox.width() * 0.3, 0.0);
        return color(center).lightness() + 48 < color(reference).lightness();
    }

    /// Returns true, if something dark (a text) is drawn in the page rectangle
    bool hasDarkPixels(const QRectF& pageRectangle)
    {
        return darkPixelsHeight(pageRectangle) > 0;
    }

    /// Returns height of the dark area (a text) drawn in the page
    /// rectangle in the pixels, zero if nothing dark is drawn there.
    int darkPixelsHeight(const QRectF& pageRectangle)
    {
        const QImage image = widget.getDrawWidget()->getWidget()->grab().toImage();
        const PDFWidgetSnapshot snapshot = widget.getDrawWidgetProxy()->getSnapshot();
        if (snapshot.items.empty())
        {
            return 0;
        }

        const QRectF deviceRectangle = snapshot.items.front().pageToDeviceMatrix.mapRect(pageRectangle);
        const qreal ratio = image.devicePixelRatio();
        const QRect rectangle = QRectF(deviceRectangle.topLeft() * ratio, deviceRectangle.size() * ratio).toRect().intersected(image.rect());

        int count = 0;
        int top = rectangle.bottom() + 1;
        int bottom = rectangle.top() - 1;
        for (int y = rectangle.top(); y <= rectangle.bottom(); ++y)
        {
            for (int x = rectangle.left(); x <= rectangle.right(); ++x)
            {
                if (image.pixelColor(x, y).lightness() < 96)
                {
                    ++count;
                    top = qMin(top, y);
                    bottom = qMax(bottom, y);
                }
            }
        }

        return count >= 16 ? bottom - top + 1 : 0;
    }

    /// Checked appearance stream of the tested documents is a green rectangle
    bool isGreen(const QPointF& pagePoint)
    {
        const QColor pixel = color(pagePoint);
        return pixel.green() > 160 && pixel.red() < 128 && pixel.blue() < 128;
    }
};

Q_DECLARE_METATYPE(FormFieldsTestHelper::Appearance)
Q_DECLARE_METATYPE(FormFieldsTestHelper::TextField)

void FormFieldsTest::checkBoxWithAppearance_data()
{
    QTest::addColumn<FormFieldsTestHelper::Appearance>("appearance");
    QTest::addColumn<QByteArray>("onState");

    QTest::newRow("both states") << FormFieldsTestHelper::Appearance::Complete << QByteArray("Yes");
    QTest::newRow("both states, custom name") << FormFieldsTestHelper::Appearance::Complete << QByteArray("1");
    QTest::newRow("checked state only") << FormFieldsTestHelper::Appearance::CheckedOnly << QByteArray("On");
}

void FormFieldsTest::checkBoxWithAppearance()
{
    QFETCH(FormFieldsTestHelper::Appearance, appearance);
    QFETCH(QByteArray, onState);

    // Check box with its own appearance of the checked state only switches the appearance
    // state, appearance streams of the document are not replaced by the library.
    FormFixture fixture(FormFieldsTestHelper::createCheckBoxDocument(appearance, onState, "Off", QByteArray(), "Off"));
    const PDFObjectReference widget = fixture.widgetReference();
    const QPointF center = FormFieldsTestHelper::CHECK_BOX.center();
    const PDFObject originalAppearance = fixture.appearance(widget);
    QVERIFY(!fixture.isGreen(center));

    fixture.click(center);
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(fixture.hasName(widget, "V", onState));
    QVERIFY(fixture.hasName(widget, "AS", onState));
    QCOMPARE(fixture.appearance(widget), originalAppearance);
    QCOMPARE(fixture.rectangle(widget), FormFieldsTestHelper::CHECK_BOX);
    QVERIFY(fixture.isGreen(center));

    fixture.click(center);
    QCOMPARE(fixture.modificationCount, 2);
    QVERIFY(fixture.hasName(widget, "V", "Off"));
    QVERIFY(fixture.hasName(widget, "AS", "Off"));
    QCOMPARE(fixture.appearance(widget), originalAppearance);
    QVERIFY(!fixture.isGreen(center));
}

void FormFieldsTest::checkBoxWithoutCheckedAppearance_data()
{
    QTest::addColumn<FormFieldsTestHelper::Appearance>("appearance");

    QTest::newRow("no appearance") << FormFieldsTestHelper::Appearance::None;
    QTest::newRow("unchecked state only") << FormFieldsTestHelper::Appearance::UncheckedOnly;
    QTest::newRow("unchecked state only, down appearance") << FormFieldsTestHelper::Appearance::UncheckedDown;
    QTest::newRow("single stream") << FormFieldsTestHelper::Appearance::SingleStream;
}

void FormFieldsTest::checkBoxWithoutCheckedAppearance()
{
    QFETCH(FormFieldsTestHelper::Appearance, appearance);

    // Check box has no appearance stream of the checked state, so there is no name
    // of this state in the document. The state 'Yes' is used (not an empty name),
    // and appearance streams are created, so the check box is displayed as checked.
    FormFixture fixture(FormFieldsTestHelper::createCheckBoxDocument(appearance, "Yes", "Off", QByteArray(), "Off"));
    const PDFObjectReference widget = fixture.widgetReference();
    const QPointF center = FormFieldsTestHelper::CHECK_BOX.center();
    const PDFObject downAppearance = fixture.downAppearance(widget);
    QVERIFY(!fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));

    // Only the normal appearance is created, other appearances of the document are kept
    fixture.click(center);
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(fixture.hasName(widget, "V", "Yes"));
    QVERIFY(fixture.hasName(widget, "AS", "Yes"));
    QCOMPARE(fixture.normalStates(widget), QByteArrayList({ "Off", "Yes" }));
    QCOMPARE(fixture.downAppearance(widget), downAppearance);
    QCOMPARE(fixture.rectangle(widget), FormFieldsTestHelper::CHECK_BOX);
    QVERIFY(fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));

    // Appearance streams are created only once
    const PDFObject createdAppearance = fixture.appearance(widget);

    fixture.click(center);
    QCOMPARE(fixture.modificationCount, 2);
    QVERIFY(fixture.hasName(widget, "V", "Off"));
    QVERIFY(fixture.hasName(widget, "AS", "Off"));
    QCOMPARE(fixture.appearance(widget), createdAppearance);
    QVERIFY(!fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));

    fixture.click(center);
    QVERIFY(fixture.hasName(widget, "V", "Yes"));
    QVERIFY(fixture.hasName(widget, "AS", "Yes"));
    QCOMPARE(fixture.appearance(widget), createdAppearance);
    QVERIFY(fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));
}

void FormFieldsTest::checkBoxCheckedStateFromValue_data()
{
    QTest::addColumn<QByteArray>("value");
    QTest::addColumn<QByteArray>("defaultValue");
    QTest::addColumn<QByteArray>("expectedValue");

    QTest::newRow("checked") << QByteArray("On") << QByteArray() << QByteArray("Off");
    QTest::newRow("checked by default") << QByteArray("Off") << QByteArray("On") << QByteArray("On");
    QTest::newRow("unchecked by default") << QByteArray("Off") << QByteArray("Off") << QByteArray("Yes");
}

void FormFieldsTest::checkBoxCheckedStateFromValue()
{
    QFETCH(QByteArray, value);
    QFETCH(QByteArray, defaultValue);
    QFETCH(QByteArray, expectedValue);

    // Check box without the appearance streams is checked by its own value, if it has
    // some - checked check box is unchecked by a single click.
    FormFixture fixture(FormFieldsTestHelper::createCheckBoxDocument(FormFieldsTestHelper::Appearance::None, "Yes", value, defaultValue, value));
    const PDFObjectReference widget = fixture.widgetReference();
    const QPointF center = FormFieldsTestHelper::CHECK_BOX.center();
    QCOMPARE(fixture.isMarked(FormFieldsTestHelper::CHECK_BOX), value != "Off");

    fixture.click(center);
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(fixture.hasName(widget, "V", expectedValue));
    QVERIFY(fixture.hasName(widget, "AS", expectedValue));
    QCOMPARE(fixture.isMarked(FormFieldsTestHelper::CHECK_BOX), expectedValue != "Off");
}

void FormFieldsTest::checkBoxWithoutAppearanceState()
{
    // Check box has neither the value, nor the appearance state - it is not checked
    FormFixture fixture(FormFieldsTestHelper::createCheckBoxDocument(FormFieldsTestHelper::Appearance::None, "Yes", QByteArray(), QByteArray(), QByteArray()));
    const PDFObjectReference widget = fixture.widgetReference();
    const QPointF center = FormFieldsTestHelper::CHECK_BOX.center();
    QVERIFY(fixture.hasName(widget, "AS", QByteArray()));
    QVERIFY(!fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));

    fixture.click(center);
    QVERIFY(fixture.hasName(widget, "V", "Yes"));
    QVERIFY(fixture.hasName(widget, "AS", "Yes"));
    QVERIFY(fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));

    fixture.click(center);
    QVERIFY(fixture.hasName(widget, "V", "Off"));
    QVERIFY(fixture.hasName(widget, "AS", "Off"));
    QVERIFY(!fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));
}

void FormFieldsTest::checkBoxCheckedStateIsKept_data()
{
    QTest::addColumn<FormFieldsTestHelper::Appearance>("appearance");
    QTest::addColumn<bool>("reopen");

    QTest::newRow("no appearance") << FormFieldsTestHelper::Appearance::None << false;
    QTest::newRow("no appearance, reopened") << FormFieldsTestHelper::Appearance::None << true;
    QTest::newRow("unchecked state only") << FormFieldsTestHelper::Appearance::UncheckedOnly << false;
    QTest::newRow("unchecked state only, reopened") << FormFieldsTestHelper::Appearance::UncheckedOnly << true;
    QTest::newRow("unchecked state only, down appearance") << FormFieldsTestHelper::Appearance::UncheckedDown << false;
    QTest::newRow("single stream") << FormFieldsTestHelper::Appearance::SingleStream << false;
    QTest::newRow("single stream, reopened") << FormFieldsTestHelper::Appearance::SingleStream << true;
}

void FormFieldsTest::checkBoxCheckedStateIsKept()
{
    QFETCH(FormFieldsTestHelper::Appearance, appearance);
    QFETCH(bool, reopen);

    // Checked check box has no appearance stream of the checked state and no default value,
    // so the name of its checked state is only in its value. The name must not be lost, when
    // the check box is unchecked - it is checked by the same value again, also if the document
    // is saved and opened between the clicks.
    std::unique_ptr<FormFixture> fixture = std::make_unique<FormFixture>(FormFieldsTestHelper::createCheckBoxDocument(appearance, "On", "On", QByteArray(), "On"));
    const QPointF center = FormFieldsTestHelper::CHECK_BOX.center();

    auto reopenDocument = [&fixture, reopen]()
    {
        bool ok = true;
        if (reopen)
        {
            PDFDocument document = FormFieldsTestHelper::reopenDocument(fixture->document.data(), ok);
            fixture.reset();
            fixture = std::make_unique<FormFixture>(std::move(document));
        }
        return ok;
    };

    for (int i = 0; i < 2; ++i)
    {
        fixture->click(center);
        QVERIFY(fixture->hasName(fixture->widgetReference(), "V", "Off"));
        QVERIFY(fixture->hasName(fixture->widgetReference(), "AS", "Off"));
        QVERIFY(!fixture->isMarked(FormFieldsTestHelper::CHECK_BOX));
        QVERIFY(reopenDocument());

        fixture->click(center);
        QVERIFY(fixture->hasName(fixture->widgetReference(), "V", "On"));
        QVERIFY(fixture->hasName(fixture->widgetReference(), "AS", "On"));
        QCOMPARE(fixture->normalStates(fixture->widgetReference()), QByteArrayList({ "Off", "On" }));
        QVERIFY(fixture->isMarked(FormFieldsTestHelper::CHECK_BOX));
        QVERIFY(reopenDocument());
    }
}

void FormFieldsTest::checkBoxUncheckedAppearanceIsKept_data()
{
    QTest::addColumn<FormFieldsTestHelper::Appearance>("appearance");
    QTest::addColumn<QByteArray>("value");

    QTest::newRow("empty") << FormFieldsTestHelper::Appearance::UncheckedOnly << QByteArray("Off");
    QTest::newRow("empty, down appearance") << FormFieldsTestHelper::Appearance::UncheckedDown << QByteArray("Off");
    QTest::newRow("green") << FormFieldsTestHelper::Appearance::UncheckedGreen << QByteArray("Off");
    QTest::newRow("green, checked") << FormFieldsTestHelper::Appearance::UncheckedGreen << QByteArray("On");
}

void FormFieldsTest::checkBoxUncheckedAppearanceIsKept()
{
    QFETCH(FormFieldsTestHelper::Appearance, appearance);
    QFETCH(QByteArray, value);

    // Check box has its own appearance stream of the unchecked state, only the appearance
    // stream of the checked state is missing. The missing stream is created, but the stream
    // of the document is neither replaced, nor drawn differently. Name of the checked
    // state is the value of the checked check box, otherwise it is the name 'Yes'.
    const QByteArray onState = value != "Off" ? value : QByteArray("Yes");
    FormFixture fixture(FormFieldsTestHelper::createCheckBoxDocument(appearance, onState, value, QByteArray(), value));
    const PDFObjectReference widget = fixture.widgetReference();
    const QPointF center = FormFieldsTestHelper::CHECK_BOX.center();
    const QPointF beside = center + QPointF(FormFieldsTestHelper::CHECK_BOX.width() * 0.3, 0.0);
    const bool isGreen = appearance == FormFieldsTestHelper::Appearance::UncheckedGreen;
    const PDFObject uncheckedAppearance = fixture.normalAppearance(widget, "Off");
    const PDFObject downAppearance = fixture.downAppearance(widget);
    QVERIFY(uncheckedAppearance.isReference());

    bool isChecked = value != "Off";
    for (int i = 0; i < 4; ++i)
    {
        fixture.click(center);
        isChecked = !isChecked;

        QVERIFY(fixture.hasName(widget, "V", isChecked ? onState : QByteArray("Off")));
        QVERIFY(fixture.hasName(widget, "AS", isChecked ? onState : QByteArray("Off")));
        QCOMPARE(fixture.normalStates(widget), QByteArrayList({ "Off", onState }));
        QCOMPARE(fixture.normalAppearance(widget, "Off"), uncheckedAppearance);
        QCOMPARE(fixture.downAppearance(widget), downAppearance);
        QCOMPARE(fixture.rectangle(widget), FormFieldsTestHelper::CHECK_BOX);

        if (isChecked)
        {
            // Checked state is drawn by the created appearance stream
            QVERIFY(fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));
            QVERIFY(!fixture.isGreen(beside));
        }
        else
        {
            // Unchecked state is drawn by the appearance stream of the document,
            // so there is no border of the check box created by the library.
            QCOMPARE(fixture.isGreen(center), isGreen);
            QCOMPARE(fixture.isGreen(beside), isGreen);
            QVERIFY(!fixture.hasDarkPixels(FormFieldsTestHelper::CHECK_BOX));
        }
    }
}

void FormFieldsTest::radioButtons()
{
    // Radio buttons are distinguished by the names of their appearance states
    FormFixture fixture(FormFieldsTestHelper::createRadioButtonDocument());
    const PDFObjectReference buttonA = fixture.widgetReference(0);
    const PDFObjectReference buttonB = fixture.widgetReference(1);
    const QPointF centerA = FormFieldsTestHelper::RADIO_BUTTON_A.center();
    const QPointF centerB = FormFieldsTestHelper::RADIO_BUTTON_B.center();
    const PDFObject appearanceA = fixture.appearance(buttonA);
    const PDFObject appearanceB = fixture.appearance(buttonB);

    PDFDocumentDataLoaderDecorator loader(fixture.document.data());
    const PDFObjectReference field = loader.readReferenceFromDictionary(fixture.dictionary(buttonA), "Parent");
    QVERIFY(field.isValid());
    QVERIFY(fixture.isGreen(centerA));
    QVERIFY(!fixture.isGreen(centerB));

    fixture.click(centerB);
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(fixture.hasName(field, "V", "B"));
    QVERIFY(fixture.hasName(buttonA, "AS", "Off"));
    QVERIFY(fixture.hasName(buttonB, "AS", "B"));
    QVERIFY(!fixture.isGreen(centerA));
    QVERIFY(fixture.isGreen(centerB));

    // Selected radio button can't be turned off
    fixture.click(centerB);
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(fixture.hasName(field, "V", "B"));
    QVERIFY(fixture.hasName(buttonB, "AS", "B"));

    fixture.click(centerA);
    QCOMPARE(fixture.modificationCount, 2);
    QVERIFY(fixture.hasName(field, "V", "A"));
    QVERIFY(fixture.hasName(buttonA, "AS", "A"));
    QVERIFY(fixture.hasName(buttonB, "AS", "Off"));
    QCOMPARE(fixture.appearance(buttonA), appearanceA);
    QCOMPARE(fixture.appearance(buttonB), appearanceB);
}

void FormFieldsTest::savedAndReopened()
{
    QByteArray data;
    {
        FormFixture fixture(FormFieldsTestHelper::createCheckBoxDocument(FormFieldsTestHelper::Appearance::SingleStream, "Yes", "Off", QByteArray(), "Off"));
        fixture.click(FormFieldsTestHelper::CHECK_BOX.center());

        QBuffer buffer(&data);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        PDFDocumentWriter writer(nullptr);
        QVERIFY(writer.write(&buffer, fixture.document.data()));
    }

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument document = reader.readFromBuffer(data);
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);

    // Checked check box is stored in the document with its appearance, so it is
    // checked also in the applications, which do not create the missing appearance.
    FormFixture fixture(std::move(document));
    const PDFObjectReference widget = fixture.widgetReference();
    const QPointF center = FormFieldsTestHelper::CHECK_BOX.center();
    QVERIFY(fixture.hasName(widget, "V", "Yes"));
    QVERIFY(fixture.hasName(widget, "AS", "Yes"));
    QCOMPARE(fixture.normalStates(widget), QByteArrayList({ "Off", "Yes" }));
    QVERIFY(fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));

    fixture.click(center);
    QVERIFY(fixture.hasName(widget, "V", "Off"));
    QVERIFY(fixture.hasName(widget, "AS", "Off"));
    QVERIFY(!fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));
}

void FormFieldsTest::textWithoutAppearance_data()
{
    QTest::addColumn<FormFieldsTestHelper::TextField>("textField");
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<bool>("needAppearances");
    QTest::addColumn<bool>("isTextDrawn");

    // Placeholders of the appearance streams, as they are written by the producers of the forms
    const QByteArray placeholder = "/Tx BMC\nEMC\n";
    const QByteArray emptyText = "/Tx BMC\nq\nBT\n0 g\n/Helv 20 Tf\nET\nQ\nEMC\n";
    const QByteArray background = "0 1 0 rg\n0 0 120 40 re f*\n/Tx BMC\nEMC\n";

    // Appearance stream with a text object - operators with the empty operands draw
    // no text, a text of the other ones is not visible (it is a space)
    auto text = [](const QByteArray& operators)
    {
        return QByteArray("/Tx BMC\nq\nBT\n0 g\n/Helv 20 Tf\n2 12 Td\n") + operators + QByteArray("\nET\nQ\nEMC\n");
    };
    const QByteArray ownText = text("( ) Tj");

    QTest::newRow("text box, placeholder") << FormFieldsTestHelper::TextField::TextBox << placeholder << true << true;
    QTest::newRow("text box, empty text object") << FormFieldsTestHelper::TextField::TextBox << emptyText << true << true;
    QTest::newRow("text box, background") << FormFieldsTestHelper::TextField::TextBox << background << true << true;
    QTest::newRow("combo box, placeholder") << FormFieldsTestHelper::TextField::ComboBox << placeholder << true << true;
    QTest::newRow("combo box, background") << FormFieldsTestHelper::TextField::ComboBox << background << true << true;
    QTest::newRow("list box, placeholder") << FormFieldsTestHelper::TextField::ListBox << placeholder << true << true;
    QTest::newRow("text box, own text") << FormFieldsTestHelper::TextField::TextBox << ownText << true << false;
    QTest::newRow("combo box, own text") << FormFieldsTestHelper::TextField::ComboBox << ownText << true << false;
    QTest::newRow("text box, empty Tj") << FormFieldsTestHelper::TextField::TextBox << text("() Tj") << true << true;
    QTest::newRow("text box, empty hexadecimal Tj") << FormFieldsTestHelper::TextField::TextBox << text("<> Tj") << true << true;
    QTest::newRow("text box, empty TJ") << FormFieldsTestHelper::TextField::TextBox << text("[] TJ") << true << true;
    QTest::newRow("text box, TJ with empty strings") << FormFieldsTestHelper::TextField::TextBox << text("[() -250 <> 120] TJ") << true << true;
    QTest::newRow("text box, empty quote") << FormFieldsTestHelper::TextField::TextBox << text("() '") << true << true;
    QTest::newRow("text box, empty double quote") << FormFieldsTestHelper::TextField::TextBox << text("0 0 () \"") << true << true;
    QTest::newRow("text box, more empty operators") << FormFieldsTestHelper::TextField::TextBox << text("() Tj\n[] TJ\n() '") << true << true;
    QTest::newRow("combo box, empty Tj") << FormFieldsTestHelper::TextField::ComboBox << text("() Tj") << true << true;
    QTest::newRow("list box, empty TJ") << FormFieldsTestHelper::TextField::ListBox << text("[] TJ") << true << true;
    QTest::newRow("text box, own text by TJ") << FormFieldsTestHelper::TextField::TextBox << text("[() -250 ( )] TJ") << true << false;
    QTest::newRow("text box, own text by quote") << FormFieldsTestHelper::TextField::TextBox << text("( ) '") << true << false;
    QTest::newRow("text box, own text by double quote") << FormFieldsTestHelper::TextField::TextBox << text("0 0 ( ) \"") << true << false;
    QTest::newRow("text box, own text after empty Tj") << FormFieldsTestHelper::TextField::TextBox << text("() Tj\n( ) Tj") << true << false;
    QTest::newRow("text box, own text before empty Tj") << FormFieldsTestHelper::TextField::TextBox << text("( ) Tj\n() Tj") << true << false;
    QTest::newRow("text box, appearance is valid") << FormFieldsTestHelper::TextField::TextBox << placeholder << false << false;
    QTest::newRow("text box, empty Tj, appearance is valid") << FormFieldsTestHelper::TextField::TextBox << text("() Tj") << false << false;
    QTest::newRow("list box, appearance is valid") << FormFieldsTestHelper::TextField::ListBox << placeholder << false << false;
}

void FormFieldsTest::textWithoutAppearance()
{
    QFETCH(FormFieldsTestHelper::TextField, textField);
    QFETCH(QByteArray, content);
    QFETCH(bool, needAppearances);
    QFETCH(bool, isTextDrawn);

    // Field has a value, but its appearance stream does not draw it. If the appearance streams
    // have to be constructed, the text is drawn from the value, the rest of the appearance
    // stream is kept and the document is not changed by the drawing.
    FormFixture fixture(FormFieldsTestHelper::createTextFieldDocument(textField, content, needAppearances));
    const PDFObjectReference widget = fixture.widgetReference();
    const PDFObject appearance = fixture.appearance(widget);
    const QPointF corner = FormFieldsTestHelper::TEXT_FIELD.topLeft() + QPointF(115, 35);

    QCOMPARE(fixture.forms.getForm()->isAppearanceUpdateNeeded(), needAppearances);
    QCOMPARE(fixture.hasDarkPixels(FormFieldsTestHelper::TEXT_FIELD), isTextDrawn);
    QCOMPARE(fixture.isGreen(corner), content.contains(" rg"));
    QCOMPARE(fixture.modificationCount, 0);
    QCOMPARE(fixture.appearance(widget), appearance);
}

void FormFieldsTest::choiceDefaultAppearance()
{
    // Default appearance of the field (font of the size 20) has a precedence over the default
    // appearance of the form (font of the size 6), so the text of the choice field is as high,
    // as the same text of the text box.
    const QByteArray placeholder = "/Tx BMC\nEMC\n";

    FormFixture textBox(FormFieldsTestHelper::createTextFieldDocument(FormFieldsTestHelper::TextField::TextBox, placeholder, true));
    const int height = textBox.darkPixelsHeight(FormFieldsTestHelper::TEXT_FIELD);
    QVERIFY(height > 0);

    for (const FormFieldsTestHelper::TextField textField : { FormFieldsTestHelper::TextField::ComboBox, FormFieldsTestHelper::TextField::ListBox })
    {
        FormFixture fixture(FormFieldsTestHelper::createTextFieldDocument(textField, placeholder, true));
        const PDFFormFieldChoice* field = dynamic_cast<const PDFFormFieldChoice*>(fixture.forms.getFormFieldForWidget(fixture.widgetReference()));
        QVERIFY(field);
        QCOMPARE(field->getDefaultAppearance(), QByteArray("0 g /Helv 20 Tf"));
        QCOMPARE(fixture.forms.getForm()->getDefaultAppearance().value_or(QByteArray()), QByteArray("0 g /Helv 6 Tf"));

        // Text of the font of the size 6 would have less than one third of the height
        const int choiceHeight = fixture.darkPixelsHeight(FormFieldsTestHelper::TEXT_FIELD);
        QVERIFY2(choiceHeight >= height * 3 / 4, qPrintable(QString("Height of the text is %1, height of the text box is %2.").arg(choiceHeight).arg(height)));
    }
}

void FormFieldsTest::inheritedDefaultAppearance_data()
{
    QTest::addColumn<FormFieldsTestHelper::TextField>("textField");
    QTest::addColumn<int>("parentCount");
    QTest::addColumn<QByteArray>("defaultAppearance");

    const QByteArray ownAppearance = "0 g /Helv 12 Tf";

    QTest::newRow("text box, parent") << FormFieldsTestHelper::TextField::TextBox << 1 << QByteArray();
    QTest::newRow("text box, two parents") << FormFieldsTestHelper::TextField::TextBox << 2 << QByteArray();
    QTest::newRow("text box, parent, own appearance") << FormFieldsTestHelper::TextField::TextBox << 1 << ownAppearance;
    QTest::newRow("text box, two parents, own appearance") << FormFieldsTestHelper::TextField::TextBox << 2 << ownAppearance;
    QTest::newRow("combo box, parent") << FormFieldsTestHelper::TextField::ComboBox << 1 << QByteArray();
    QTest::newRow("combo box, two parents") << FormFieldsTestHelper::TextField::ComboBox << 2 << QByteArray();
    QTest::newRow("combo box, three parents") << FormFieldsTestHelper::TextField::ComboBox << 3 << QByteArray();
    QTest::newRow("combo box, parent, own appearance") << FormFieldsTestHelper::TextField::ComboBox << 1 << ownAppearance;
    QTest::newRow("combo box, two parents, own appearance") << FormFieldsTestHelper::TextField::ComboBox << 2 << ownAppearance;
    QTest::newRow("list box, parent") << FormFieldsTestHelper::TextField::ListBox << 1 << QByteArray();
    QTest::newRow("list box, two parents") << FormFieldsTestHelper::TextField::ListBox << 2 << QByteArray();
    QTest::newRow("list box, parent, own appearance") << FormFieldsTestHelper::TextField::ListBox << 1 << ownAppearance;
}

void FormFieldsTest::inheritedDefaultAppearance()
{
    QFETCH(FormFieldsTestHelper::TextField, textField);
    QFETCH(int, parentCount);
    QFETCH(QByteArray, defaultAppearance);

    // Default appearance (font of the size 20) and the type of the field are defined by the root
    // field, the form has a default appearance with the font of the size 6. The field inherits
    // the default appearance of the root field, if it has no default appearance of its own.
    const QByteArray placeholder = "/Tx BMC\nEMC\n";
    const QByteArray inheritedAppearance = "0 g /Helv 20 Tf";
    const QByteArray expectedAppearance = defaultAppearance.isEmpty() ? inheritedAppearance : defaultAppearance;

    FormFixture reference(FormFieldsTestHelper::createTextFieldDocument(FormFieldsTestHelper::TextField::TextBox, placeholder, true));
    const int referenceHeight = reference.darkPixelsHeight(FormFieldsTestHelper::TEXT_FIELD);
    QVERIFY(referenceHeight > 0);

    FormFixture fixture(FormFieldsTestHelper::createTextFieldDocument(textField, placeholder, true, defaultAppearance, parentCount));
    const PDFFormField* field = fixture.forms.getFormFieldForWidget(fixture.widgetReference());
    QVERIFY(field);
    QCOMPARE(fixture.forms.getForm()->getDefaultAppearance().value_or(QByteArray()), QByteArray("0 g /Helv 6 Tf"));

    int level = 0;
    for (const PDFFormField* parentField = field->getParentField(); parentField; parentField = parentField->getParentField())
    {
        ++level;
    }
    QCOMPARE(level, parentCount);

    if (textField == FormFieldsTestHelper::TextField::TextBox)
    {
        const PDFFormFieldText* textFormField = dynamic_cast<const PDFFormFieldText*>(field);
        QVERIFY(textFormField);
        QCOMPARE(textFormField->getDefaultAppearance(), expectedAppearance);
    }
    else
    {
        const PDFFormFieldChoice* choiceFormField = dynamic_cast<const PDFFormFieldChoice*>(field);
        QVERIFY(choiceFormField);
        QCOMPARE(choiceFormField->isComboBox(), textField == FormFieldsTestHelper::TextField::ComboBox);
        QCOMPARE(choiceFormField->getDefaultAppearance(), expectedAppearance);
    }

    // Text of the font of the size 20 is as high, as the text of the text box with the same font,
    // text of the font of the size 12 is lower, but it is higher, than the text of the size 6.
    const int height = fixture.darkPixelsHeight(FormFieldsTestHelper::TEXT_FIELD);
    const QString message = QString("Height of the text is %1, height of the text of the size 20 is %2.").arg(height).arg(referenceHeight);
    if (defaultAppearance.isEmpty())
    {
        QVERIFY2(height * 4 >= referenceHeight * 3, qPrintable(message));
    }
    else
    {
        QVERIFY2(height * 4 < referenceHeight * 3 && height * 20 > referenceHeight * 9, qPrintable(message));
    }
}

void FormFieldsTest::tabOrder_data()
{
    QTest::addColumn<QByteArray>("tabs");
    QTest::addColumn<QStringList>("order");

    QTest::newRow("annotation order") << QByteArray() << QStringList({ "C", "A", "B", "D" });
    QTest::newRow("structure (annotation order)") << QByteArray("S") << QStringList({ "C", "A", "B", "D" });
    QTest::newRow("rows") << QByteArray("R") << QStringList({ "B", "A", "C", "D" });
    QTest::newRow("columns") << QByteArray("C") << QStringList({ "B", "C", "A", "D" });
}

void FormFieldsTest::tabOrder()
{
    QFETCH(QByteArray, tabs);
    QFETCH(QStringList, order);

    FormFixture fixture(FormFieldsTestHelper::createTabOrderDocument(tabs));

    QStringList names;
    for (const PDFFormWidget& formWidget : fixture.forms.getWidgetsInTabOrder())
    {
        names << formWidget.getParent()->getName(PDFFormField::FullyQualified);
    }
    QCOMPARE(names, order);

    // Tab key moves the focus in the same order
    QStringList focused;
    for (int i = 0; i < order.size(); ++i)
    {
        QVERIFY(fixture.forms.focusNextPrevFormField(true));
        for (const PDFFormWidget& formWidget : fixture.forms.getWidgets())
        {
            if (fixture.forms.isFocused(formWidget.getWidget()))
            {
                focused << formWidget.getParent()->getName(PDFFormField::FullyQualified);
            }
        }
    }
    QCOMPARE(focused, order);

    // Focus leaves the form after the last field
    QVERIFY(!fixture.forms.focusNextPrevFormField(true));
}

void FormFieldsTest::tabSkipsReadOnlyFields()
{
    FormFixture fixture(FormFieldsTestHelper::createTabOrderDocument(QByteArray(), "A"));

    auto focusedName = [&fixture]() -> QString
    {
        for (const PDFFormWidget& formWidget : fixture.forms.getWidgets())
        {
            if (fixture.forms.isFocused(formWidget.getWidget()))
            {
                return formWidget.getParent()->getName(PDFFormField::FullyQualified);
            }
        }
        return QString();
    };

    QVERIFY(fixture.forms.focusNextPrevFormField(true));
    QCOMPARE(focusedName(), QString("C"));
    QVERIFY(fixture.forms.focusNextPrevFormField(true));
    QCOMPARE(focusedName(), QString("B"));
    QVERIFY(fixture.forms.focusNextPrevFormField(false));
    QCOMPARE(focusedName(), QString("C"));
}

void FormFieldsTest::tabSkipsHiddenFields_data()
{
    QTest::addColumn<int>("flags");

    QTest::newRow("hidden") << int(PDFAnnotation::Hidden);
    QTest::newRow("no view") << int(PDFAnnotation::NoView);
}

void FormFieldsTest::tabSkipsHiddenFields()
{
    QFETCH(int, flags);

    FormFixture fixture(FormFieldsTestHelper::createTabOrderDocument(QByteArray(), QString(), "A", flags));

    auto focusedName = [&fixture]() -> QString
    {
        for (const PDFFormWidget& formWidget : fixture.forms.getWidgets())
        {
            if (fixture.forms.isFocused(formWidget.getWidget()))
            {
                return formWidget.getParent()->getName(PDFFormField::FullyQualified);
            }
        }
        return QString();
    };

    // Annotation order of the first page is C, A, B; A is hidden
    QVERIFY(fixture.forms.focusNextPrevFormField(true));
    QCOMPARE(focusedName(), QString("C"));
    QVERIFY(fixture.forms.focusNextPrevFormField(true));
    QCOMPARE(focusedName(), QString("B"));
    QVERIFY(fixture.forms.focusNextPrevFormField(true));
    QCOMPARE(focusedName(), QString("D"));
    QVERIFY(fixture.forms.focusNextPrevFormField(false));
    QCOMPARE(focusedName(), QString("B"));
    QVERIFY(fixture.forms.focusNextPrevFormField(false));
    QCOMPARE(focusedName(), QString("C"));
}

void FormFieldsTest::flattenText_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<bool>("needAppearances");

    QTest::newRow("own appearance") << QByteArray("/Tx BMC BT /Helv 20 Tf 0 g 4 12 Td (Hello) Tj ET EMC") << false;
    QTest::newRow("need appearances") << QByteArray("/Tx BMC EMC") << true;
}

void FormFieldsTest::flattenText()
{
    QFETCH(QByteArray, content);
    QFETCH(bool, needAppearances);

    PDFDocument flattened;
    {
        FormFixture fixture(FormFieldsTestHelper::createTextFieldDocument(FormFieldsTestHelper::TextField::TextBox, content, needAppearances));
        QVERIFY(fixture.hasDarkPixels(FormFieldsTestHelper::TEXT_FIELD));
        flattened = fixture.forms.createFlattenedDocument();
    }

    bool ok = false;
    PDFDocument reopened = FormFieldsTestHelper::reopenDocument(&flattened, ok);
    QVERIFY(ok);

    // Form and widgets are removed, text is a part of the page content
    QVERIFY(reopened.getCatalog()->getFormObject().isNull());
    QVERIFY(reopened.getCatalog()->getPage(0)->getAnnotations().empty());

    // Page content is compiled asynchronously (unlike the annotations), so we wait for it
    FormFixture fixture(std::move(reopened));
    QVERIFY(!fixture.forms.hasAcroForm());
    QTRY_VERIFY_WITH_TIMEOUT(fixture.hasDarkPixels(FormFieldsTestHelper::TEXT_FIELD), 10000);
    QVERIFY(!fixture.hasDarkPixels(QRectF(0, 0, 30, 30)));
}

void FormFieldsTest::flattenCheckBox()
{
    for (const QByteArray& state : { QByteArray("Yes"), QByteArray("Off") })
    {
        FormFixture fixture(FormFieldsTestHelper::createCheckBoxDocument(FormFieldsTestHelper::Appearance::Complete, "Yes", state, QByteArray(), state));
        QCOMPARE(fixture.isGreen(FormFieldsTestHelper::CHECK_BOX.center()), state == "Yes");

        PDFDocument flattened = fixture.forms.createFlattenedDocument();
        QVERIFY(flattened.getCatalog()->getPage(0)->getAnnotations().empty());

        // Page content is compiled asynchronously (unlike the annotations), so we wait for it
        FormFixture flattenedFixture(std::move(flattened));
        if (state == "Yes")
        {
            QTRY_VERIFY_WITH_TIMEOUT(flattenedFixture.isGreen(FormFieldsTestHelper::CHECK_BOX.center()), 10000);
        }
        else
        {
            QTest::qWait(200);
            QVERIFY(!flattenedFixture.isGreen(FormFieldsTestHelper::CHECK_BOX.center()));
        }

        // Clicking on the flattened check box does nothing
        flattenedFixture.click(FormFieldsTestHelper::CHECK_BOX.center());
        QCOMPARE(flattenedFixture.modificationCount, 0);
    }
}

void FormFieldsTest::flattenKeepsOtherAnnotations()
{
    PDFDocument document = FormFieldsTestHelper::createCheckBoxDocument(FormFieldsTestHelper::Appearance::Complete, "Yes", "Yes", QByteArray(), "Yes");

    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = builder.getPages().front();
    const PDFObjectReference note = builder.createAnnotationText(page, QRectF(10, 10, 20, 20), TextAnnotationIcon::Note, "Author", "Subject", "Comment", false);
    document = builder.build();

    FormFixture fixture(std::move(document));
    const PDFObjectReference widget = fixture.widgetReference();
    QCOMPARE(fixture.document->getCatalog()->getPage(0)->getAnnotations().size(), size_t(3)); // widget, note, popup of the note

    PDFDocument flattened = fixture.forms.createFlattenedDocument();
    const std::vector<PDFObjectReference>& annotations = flattened.getCatalog()->getPage(0)->getAnnotations();
    QCOMPARE(annotations.size(), size_t(2));
    QVERIFY(std::find(annotations.cbegin(), annotations.cend(), note) != annotations.cend());
    QVERIFY(std::find(annotations.cbegin(), annotations.cend(), widget) == annotations.cend());
}

void FormFieldsTest::flattenRemovesHiddenValuesAndScripts()
{
    PDFDocument document = FormFieldsTestHelper::createTabOrderDocument(QByteArray(), QString(), "B", PDFAnnotation::Hidden, "Hidden secret value");

    // Document scripts: open action and named JavaScript
    {
        PDFDocumentBuilder builder(&document);
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("OpenAction");
        factory.beginDictionary();
        FormFieldsTestHelper::addName(factory, "S", "JavaScript");
        factory.beginDictionaryItem("JS");
        factory << WrapString("app.alert('open script')");
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Names");
        factory.beginDictionary();
        factory.beginDictionaryItem("JavaScript");
        factory.beginDictionary();
        factory.beginDictionaryItem("Names");
        factory.beginArray();
        factory << WrapString("init");
        factory.beginDictionary();
        FormFieldsTestHelper::addName(factory, "S", "JavaScript");
        factory.beginDictionaryItem("JS");
        factory << WrapString("app.alert('named script')");
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endArray();
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(builder.getCatalogReference(), factory.takeObject());
        document = builder.build();
    }

    PDFDocument flattened;
    {
        FormFixture fixture(document);
        flattened = fixture.forms.createFlattenedDocument();
    }

    QByteArray data;
    QBuffer buffer(&data);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    PDFDocumentWriter writer(nullptr);
    QVERIFY(bool(writer.write(&buffer, &flattened)));
    buffer.close();

    QVERIFY(!data.contains("Hidden secret value"));
    QVERIFY(!data.contains("open script"));
    QVERIFY(!data.contains("named script"));
    QVERIFY(!data.contains("/AcroForm"));

    // Flattened document is still valid
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument reopened = reader.readFromBuffer(data);
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);
    QCOMPARE(reopened.getCatalog()->getPageCount(), size_t(2));
}

void FormFieldsTest::autocomplete()
{
    PDFFormFieldValueHistory::clear();
    PDFFormFieldValueHistory::addValue("Other", "2024-11-03 Rev B");
    PDFFormFieldValueHistory::addValue("Text", "Hurst Lighting");
    PDFFormFieldValueHistory::addValue("Text", "Hello World");
    PDFFormFieldValueHistory::addValue("Text", "Help Desk");

    // Values of the same field are suggested, the most recent first
    QCOMPARE(PDFFormFieldValueHistory::complete("Text", "he"), QString("Help Desk"));
    QCOMPARE(PDFFormFieldValueHistory::complete("Text", "Hell"), QString("Hello World"));
    QCOMPARE(PDFFormFieldValueHistory::complete("Text", "Hu"), QString("Hurst Lighting"));
    QCOMPARE(PDFFormFieldValueHistory::complete("Text", "X"), QString());
    QCOMPARE(PDFFormFieldValueHistory::complete("Text", "Help Desk"), QString());

    // Values of other fields are never suggested
    QCOMPARE(PDFFormFieldValueHistory::complete("Text", "2"), QString());
    QCOMPARE(PDFFormFieldValueHistory::complete("Other", "2"), QString("2024-11-03 Rev B"));
    QCOMPARE(PDFFormFieldValueHistory::complete(QString(), "2"), QString());

    // Fields holding secrets are never remembered
    QVERIFY(PDFFormFieldValueHistory::isSensitiveField("Account Number"));
    QVERIFY(PDFFormFieldValueHistory::isSensitiveField("form.CardNumber"));
    QVERIFY(PDFFormFieldValueHistory::isSensitiveField("Password"));
    QVERIFY(PDFFormFieldValueHistory::isSensitiveField("SSN"));
    QVERIFY(!PDFFormFieldValueHistory::isSensitiveField("Project"));
    QVERIFY(!PDFFormFieldValueHistory::isSensitiveField("Catalog_Number"));
    QVERIFY(!PDFFormFieldValueHistory::isSensitiveField("Spinner"));
    PDFFormFieldValueHistory::addValue("Account Number", "123456789");
    QCOMPARE(PDFFormFieldValueHistory::complete("Account Number", "1"), QString());

    FormFixture fixture(FormFieldsTestHelper::createTextFieldDocument(FormFieldsTestHelper::TextField::TextBox, "/Tx BMC EMC", true));
    QWidget* target = fixture.widget.getDrawWidget()->getWidget();
    const PDFObjectReference widget = fixture.widgetReference();
    const PDFFormField* formField = fixture.forms.getFormFieldForWidget(widget);
    QVERIFY(formField);

    auto fieldValue = [&fixture, widget]()
    {
        PDFDocumentDataLoaderDecorator loader(fixture.document.data());
        return loader.readTextString(fixture.forms.getFormFieldForWidget(widget)->getValue(), QString());
    };

    // Typed letters are completed by the remembered value, Enter accepts it
    fixture.click(FormFieldsTestHelper::TEXT_FIELD.center());
    QTest::keyClick(target, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(target, Qt::Key_Backspace);
    QTest::keyClicks(target, "hu");
    QTest::keyClick(target, Qt::Key_Return);
    QCOMPARE(fieldValue(), QString("hurst Lighting"));

    // Backspace removes the suggestion
    fixture.click(FormFieldsTestHelper::TEXT_FIELD.center());
    QTest::keyClick(target, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(target, Qt::Key_Backspace);
    QTest::keyClicks(target, "Hel");
    QTest::keyClick(target, Qt::Key_Backspace);
    QTest::keyClick(target, Qt::Key_Return);
    QCOMPARE(fieldValue(), QString("Hel"));

    // Typing over the suggestion continues normally
    fixture.click(FormFieldsTestHelper::TEXT_FIELD.center());
    QTest::keyClick(target, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(target, Qt::Key_Backspace);
    QTest::keyClicks(target, "Hex");
    QTest::keyClick(target, Qt::Key_Return);
    QCOMPARE(fieldValue(), QString("Hex"));

    // Committed values are remembered
    QCOMPARE(PDFFormFieldValueHistory::complete("Text", "He"), QString("Hex"));

    // Disabled autocomplete doesn't suggest anything
    fixture.forms.setAutocompleteEnabled(false);
    fixture.click(FormFieldsTestHelper::TEXT_FIELD.center());
    QTest::keyClick(target, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(target, Qt::Key_Backspace);
    QTest::keyClicks(target, "Hu");
    QTest::keyClick(target, Qt::Key_Return);
    QCOMPARE(fieldValue(), QString("Hu"));

    PDFFormFieldValueHistory::clear();
    QCOMPARE(PDFFormFieldValueHistory::complete("Text", "He"), QString());
}

void FormFieldsTest::commitPendingChanges()
{
    PDFFormFieldValueHistory::clear();
    PDFFormFieldValueHistory::addValue("Text", "Hurst Lighting");

    FormFixture fixture(FormFieldsTestHelper::createTextFieldDocument(FormFieldsTestHelper::TextField::TextBox, "/Tx BMC EMC", true));
    QWidget* target = fixture.widget.getDrawWidget()->getWidget();
    const PDFObjectReference widget = fixture.widgetReference();

    auto fieldValue = [&fixture, widget]()
    {
        PDFDocumentDataLoaderDecorator loader(fixture.document.data());
        return loader.readTextString(fixture.forms.getFormFieldForWidget(widget)->getValue(), QString());
    };

    // Text being typed is written into the document (before saving),
    // the field stays focused and typing continues
    fixture.click(FormFieldsTestHelper::TEXT_FIELD.center());
    QTest::keyClick(target, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(target, Qt::Key_Backspace);
    QTest::keyClicks(target, "Ab");
    fixture.forms.commitPendingChanges();
    QCOMPARE(fieldValue(), QString("Ab"));
    QVERIFY(fixture.forms.isFocused(widget));
    QTest::keyClicks(target, "c");
    fixture.forms.commitPendingChanges();
    QCOMPARE(fieldValue(), QString("Abc"));
    QTest::keyClick(target, Qt::Key_Left);
    QTest::keyClicks(target, "X");
    QTest::keyClick(target, Qt::Key_Return);
    QCOMPARE(fieldValue(), QString("AbXc"));

    // Suggestion, which the user didn't accept, is not written
    fixture.click(FormFieldsTestHelper::TEXT_FIELD.center());
    QTest::keyClick(target, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(target, Qt::Key_Backspace);
    QTest::keyClicks(target, "Hu");
    fixture.forms.commitPendingChanges();
    QCOMPARE(fieldValue(), QString("Hu"));

    // ... and the user can still accept it
    QTest::keyClick(target, Qt::Key_Return);
    QCOMPARE(fieldValue(), QString("Hurst Lighting"));

    // Without focused field, nothing happens
    fixture.forms.commitPendingChanges();
    QCOMPARE(fieldValue(), QString("Hurst Lighting"));

    PDFFormFieldValueHistory::clear();
}

void FormFieldsTest::typewriter()
{
    // Action must outlive the fixture, which deactivates the tools in its destructor
    QAction action;
    action.setCheckable(true);

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(QPointF(0, 0), FormFieldsTestHelper::PAGE_SIZE));
    FormFixture fixture(builder.build());
    PDFCreateTypewriterTool* tool = new PDFCreateTypewriterTool(fixture.widget.getDrawWidgetProxy(), &fixture.tools, &action, &fixture.widget);
    fixture.tools.addTool(tool);
    QObject::connect(&fixture.tools, &PDFToolManager::documentModified, &fixture.tools, [&fixture](PDFModifiedDocument modified)
    {
        fixture.document = modified;
        fixture.apply(PDFModifiedDocument(fixture.document, nullptr, modified.getFlags()));
    });
    fixture.apply(PDFModifiedDocument(fixture.document, nullptr));
    tool->setActive(true);

    // Empty text creates nothing
    const QPointF pagePoint(40, 150);
    QVERIFY(tool->beginText(fixture.device(pagePoint)));
    QVERIFY(tool->getEditor());
    tool->commitText();
    QVERIFY(!tool->getEditor());
    QVERIFY(fixture.document->getCatalog()->getPage(0)->getAnnotations().empty());

    // Typed text is a typewriter free text annotation, its top left corner is at the clicked point
    QVERIFY(tool->beginText(fixture.device(pagePoint)));
    tool->getEditor()->setPlainText("Hello\nWorld\n");
    tool->commitText();

    const std::vector<PDFObjectReference>& annotations = fixture.document->getCatalog()->getPage(0)->getAnnotations();
    QCOMPARE(annotations.size(), size_t(1));
    const PDFObjectReference annotation = annotations.front();
    QVERIFY(fixture.hasName(annotation, "Subtype", "FreeText"));
    QVERIFY(fixture.hasName(annotation, "IT", "FreeTextTypeWriter"));

    PDFDocumentDataLoaderDecorator loader(fixture.document.data());
    QCOMPARE(loader.readTextString(fixture.dictionary(annotation)->get("Contents"), QString()), QString("Hello\nWorld"));

    const QRectF rectangle = fixture.rectangle(annotation).normalized();
    QVERIFY(qAbs(rectangle.left() - pagePoint.x()) < 2.0);
    QVERIFY(qAbs(rectangle.bottom() - pagePoint.y()) < 2.0);
    QVERIFY(rectangle.height() > 20.0);
    QVERIFY(fixture.hasDarkPixels(rectangle));

    // No border is drawn around the text
    QVERIFY(!fixture.hasDarkPixels(QRectF(rectangle.left(), rectangle.top(), rectangle.width(), 1.0).adjusted(0, -0.5, 0, 0.5)));

    // Escape without text finishes the tool
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    tool->keyPressEvent(fixture.widget.getDrawWidget()->getWidget(), &escape);
    QVERIFY(!tool->isActive());
}

void FormFieldsTest::recoveryCopy()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    pdfviewer::PDFRecoveryManager manager(directory.filePath("Recovery"));
    const QString fileName = directory.filePath("Spec sheet.pdf");
    PDFDocument document = FormFieldsTestHelper::createCheckBoxDocument(FormFieldsTestHelper::Appearance::Complete, "Yes", "Yes", QByteArray(), "Yes");

    QFile original(fileName);
    QVERIFY(original.open(QIODevice::WriteOnly));
    original.write("%PDF-1.7");
    original.close();
    const QDateTime lastModified = QFileInfo(fileName).lastModified();

    QVERIFY(!manager.findRecoveryItem(fileName));
    QVERIFY(manager.getRecoveryItems().empty());

    QVERIFY(manager.lockRecoveryCopy(fileName));
    QVERIFY(manager.writeRecoveryCopy(fileName, lastModified, &document));
    std::optional<pdfviewer::PDFRecoveryManager::RecoveryItem> item = manager.findRecoveryItem(fileName);
    QVERIFY(item);
    QCOMPARE(QFileInfo(item->originalFileName), QFileInfo(fileName));
    QVERIFY(item->time.isValid());
    QCOMPARE(item->originalLastModified, lastModified);
    QVERIFY(!item->offered);
    QVERIFY(!item->isOriginalModified());
    QCOMPARE(manager.getRecoveryItems().size(), size_t(1));

    // Recovery copy is a valid document with the same content
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument recovered = reader.readFromFile(item->recoveryFileName);
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);
    QCOMPARE(recovered.getCatalog()->getPageCount(), size_t(1));
    QVERIFY(!recovered.getCatalog()->getFormObject().isNull());

    // Original changed after the copy was made (saved by another application)
    QVERIFY(original.open(QIODevice::WriteOnly));
    original.write("%PDF-1.7 changed");
    QVERIFY(original.setFileTime(lastModified.addSecs(60), QFileDevice::FileModificationTime));
    original.close();
    QVERIFY(manager.findRecoveryItem(fileName)->isOriginalModified());

    // Other document has its own recovery copy
    const QString otherFileName = directory.filePath("Other.pdf");
    QVERIFY(manager.lockRecoveryCopy(otherFileName));
    QVERIFY(manager.writeRecoveryCopy(otherFileName, QDateTime(), &document));
    QCOMPARE(manager.getRecoveryItems().size(), size_t(2));
    QVERIFY(!manager.findRecoveryItem(otherFileName)->isOriginalModified());

    manager.removeRecoveryCopy(fileName);
    QVERIFY(!manager.findRecoveryItem(fileName));
    QVERIFY(manager.findRecoveryItem(otherFileName));
    QCOMPARE(manager.getRecoveryItems().size(), size_t(1));
    QVERIFY(!QFileInfo::exists(item->recoveryFileName));
    QVERIFY(!QFileInfo::exists(QString(item->recoveryFileName).replace(".pdf", ".lock")));
}

void FormFieldsTest::recoveryCopyLockedByOtherWindow()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString recoveryDirectory = directory.filePath("Recovery");
    const QString fileName = directory.filePath("Spec sheet.pdf");
    PDFDocument document = FormFieldsTestHelper::createCheckBoxDocument(FormFieldsTestHelper::Appearance::Complete, "Yes", "Yes", QByteArray(), "Yes");

    // First window writes the recovery copy and holds its lock
    pdfviewer::PDFRecoveryManager firstWindow(recoveryDirectory);
    QVERIFY(firstWindow.lockRecoveryCopy(fileName));
    QVERIFY(firstWindow.writeRecoveryCopy(fileName, QDateTime(), &document));
    const QString recoveryFileName = firstWindow.findRecoveryItem(fileName)->recoveryFileName;

    // Second window doesn't offer it, can't remove it, can't overwrite it,
    // and doesn't consider it invalid
    pdfviewer::PDFRecoveryManager secondWindow(recoveryDirectory);
    QVERIFY(!secondWindow.findRecoveryItem(fileName));
    QVERIFY(secondWindow.getRecoveryItems().empty());
    QVERIFY(!secondWindow.lockRecoveryCopy(fileName));
    secondWindow.removeRecoveryCopy(fileName);
    secondWindow.removeInvalidRecoveryCopies();
    QVERIFY(QFileInfo::exists(recoveryFileName));
    QVERIFY(firstWindow.findRecoveryItem(fileName));

    // When the first window releases the lock (closes, or crashes and the system
    // releases its lock), the second window can recover the copy
    firstWindow = pdfviewer::PDFRecoveryManager(recoveryDirectory);
    QVERIFY(secondWindow.findRecoveryItem(fileName));
    QVERIFY(secondWindow.lockRecoveryCopy(fileName));
    secondWindow.removeRecoveryCopy(fileName);
    QVERIFY(!QFileInfo::exists(recoveryFileName));
}

void FormFieldsTest::recoveryCopyOfferedAndCleanup()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString recoveryDirectory = directory.filePath("Recovery");
    const QString fileName = directory.filePath("Spec sheet.pdf");
    PDFDocument document = FormFieldsTestHelper::createCheckBoxDocument(FormFieldsTestHelper::Appearance::Complete, "Yes", "Yes", QByteArray(), "Yes");

    {
        pdfviewer::PDFRecoveryManager crashedWindow(recoveryDirectory);
        QVERIFY(crashedWindow.lockRecoveryCopy(fileName));
        QVERIFY(crashedWindow.writeRecoveryCopy(fileName, QDateTime(), &document));
    }

    // Copy of the crashed window is not released by another window
    // (the document was opened and closed without a decision)
    pdfviewer::PDFRecoveryManager manager(recoveryDirectory);
    manager.releaseRecoveryCopy(fileName);
    QVERIFY(manager.findRecoveryItem(fileName));

    // User postponed the recovery, copy is kept, but marked as offered
    manager.setOffered(fileName);
    std::optional<pdfviewer::PDFRecoveryManager::RecoveryItem> item = manager.findRecoveryItem(fileName);
    QVERIFY(item);
    QVERIFY(item->offered);

    // New recovery copy of the document is offered again
    QVERIFY(manager.lockRecoveryCopy(fileName));
    QVERIFY(manager.writeRecoveryCopy(fileName, QDateTime(), &document));
    QVERIFY(!manager.findRecoveryItem(fileName)->offered);
    manager = pdfviewer::PDFRecoveryManager(recoveryDirectory);

    // Incomplete copies (copy without description, description without copy)
    // and copies older than the limit are removed, valid copy is kept
    QDir recovery(recoveryDirectory);
    QFile orphanCopy(recovery.absoluteFilePath("0123456789abcdef.pdf"));
    QVERIFY(orphanCopy.open(QIODevice::WriteOnly));
    orphanCopy.close();
    QSettings orphanDescription(recovery.absoluteFilePath("fedcba9876543210.ini"), QSettings::IniFormat);
    orphanDescription.setValue("OriginalFileName", directory.filePath("Missing.pdf"));
    orphanDescription.sync();

    const QString oldFileName = directory.filePath("Old.pdf");
    QVERIFY(manager.lockRecoveryCopy(oldFileName));
    QVERIFY(manager.writeRecoveryCopy(oldFileName, QDateTime(), &document));
    const QString oldRecoveryFileName = manager.findRecoveryItem(oldFileName)->recoveryFileName;
    {
        QSettings oldDescription(QString(oldRecoveryFileName).replace(".pdf", ".ini"), QSettings::IniFormat);
        oldDescription.setValue("Time", QDateTime::currentDateTime().addDays(-pdfviewer::PDFRecoveryManager::MAX_AGE_DAYS - 1));
    }
    manager = pdfviewer::PDFRecoveryManager(recoveryDirectory);

    manager.removeInvalidRecoveryCopies();
    QVERIFY(!QFileInfo::exists(orphanCopy.fileName()));
    QVERIFY(!QFileInfo::exists(recovery.absoluteFilePath("fedcba9876543210.ini")));
    QVERIFY(!QFileInfo::exists(oldRecoveryFileName));
    QVERIFY(manager.findRecoveryItem(fileName));
    QCOMPARE(manager.getRecoveryItems().size(), size_t(1));
}

void FormFieldsTest::comboBoxKeyboard_data()
{
    QTest::addColumn<bool>("optionsInParent");

    QTest::newRow("options in the field") << false;
    QTest::newRow("options in the parent field") << true;
}

void FormFieldsTest::comboBoxKeyboard()
{
    QFETCH(bool, optionsInParent);

    PDFDocument document = FormFieldsTestHelper::createTextFieldDocument(FormFieldsTestHelper::TextField::ComboBox, "/Tx BMC EMC", true, "0 g /Helv 20 Tf", optionsInParent ? 1 : 0);
    if (optionsInParent)
    {
        // Widget annotation is a kid of the field, which defines the options
        PDFDocumentBuilder builder(&document);
        const PDFObjectReference widget = document.getCatalog()->getPage(0)->getAnnotations().front();
        const PDFDictionary* widgetDictionary = document.getDictionaryFromObject(document.getObjectByReference(widget));
        PDFDocumentDataLoaderDecorator loader(&document);
        const PDFObjectReference parent = loader.readReferenceFromDictionary(widgetDictionary, "Parent");

        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Opt");
        factory << widgetDictionary->get("Opt");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("V");
        factory << QString("Hello");
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(parent, factory.takeObject());

        PDFDictionaryBuilder newWidget(*widgetDictionary);
        newWidget.removeEntry("Opt");
        newWidget.removeEntry("V");
        newWidget.removeEntry("T");
        builder.setObject(widget, PDFObject::createDictionary(std::move(newWidget)));
        document = builder.build();
    }

    FormFixture fixture(std::move(document));
    QWidget* target = fixture.widget.getDrawWidget()->getWidget();
    const PDFObjectReference widget = fixture.widgetReference();

    auto fieldValue = [&fixture, widget]()
    {
        PDFDocumentDataLoaderDecorator loader(fixture.document.data());
        const PDFFormField* field = fixture.forms.getFormFieldForWidget(widget);
        while (field && field->getValue().isNull() && field->getParentField())
        {
            field = field->getParentField();
        }
        return loader.readTextString(field->getValue(), QString());
    };

    QCOMPARE(fieldValue(), QString("Hello"));

    // Down selects the next option
    QVERIFY(fixture.forms.focusNextPrevFormField(true));
    QTest::keyClick(target, Qt::Key_Down);
    QTest::keyClick(target, Qt::Key_Return);
    QCOMPARE(fieldValue(), QString("World"));

    // Up selects the previous option, the first option stays selected
    QVERIFY(fixture.forms.focusNextPrevFormField(true));
    QTest::keyClick(target, Qt::Key_Up);
    QTest::keyClick(target, Qt::Key_Up);
    QTest::keyClick(target, Qt::Key_Return);
    QCOMPARE(fieldValue(), QString("Hello"));

    // Typed letter selects the option starting with it
    QVERIFY(fixture.forms.focusNextPrevFormField(true));
    QTest::keyClicks(target, "w");
    QTest::keyClick(target, Qt::Key_Return);
    QCOMPARE(fieldValue(), QString("World"));
}

void FormFieldsTest::fillRealForm()
{
    // Fills a real form by the keyboard only: each text field gets the text
    // '<field name> value', Tab moves through the fields, Space checks the check boxes
    // and the arrow key changes the combo box. The filled document is saved as
    // '<output>-filled.pdf' and its flattened copy as '<output>-flattened.pdf', so
    // they can be checked in other applications.
    const QString inputFileName = qEnvironmentVariable("PDF4QT_FORM_TEST_FILE");
    const QString outputFileName = qEnvironmentVariable("PDF4QT_FORM_TEST_OUTPUT");
    if (inputFileName.isEmpty() || outputFileName.isEmpty())
    {
        QSKIP("Set PDF4QT_FORM_TEST_FILE (form to fill) and PDF4QT_FORM_TEST_OUTPUT (output file name prefix)");
    }

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument document = reader.readFromFile(inputFileName);
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);

    PDFFormFieldValueHistory::clear();
    FormFixture fixture(std::move(document));
    QWidget* target = fixture.widget.getDrawWidget()->getWidget();
    QVERIFY(fixture.forms.hasAcroForm());

    auto focusedWidget = [&fixture]() -> std::optional<PDFFormWidget>
    {
        for (const PDFFormWidget& formWidget : fixture.forms.getWidgets())
        {
            if (fixture.forms.isFocused(formWidget.getWidget()))
            {
                return formWidget;
            }
        }
        return std::nullopt;
    };

    QStringList order;
    QVERIFY(fixture.forms.focusNextPrevFormField(true));
    for (std::optional<PDFFormWidget> formWidget = focusedWidget(); formWidget; formWidget = focusedWidget())
    {
        const PDFFormField* formField = formWidget->getParent();
        const QString name = formField->getName(PDFFormField::FullyQualified);
        order << name;

        switch (formField->getFieldType())
        {
            case PDFFormField::FieldType::Text:
                QTest::keyClick(target, Qt::Key_A, Qt::ControlModifier);
                QTest::keyClicks(target, name + " value");
                break;

            case PDFFormField::FieldType::Button:
                QTest::keyClick(target, Qt::Key_Space);
                break;

            case PDFFormField::FieldType::Choice:
                QTest::keyClick(target, Qt::Key_Down);
                break;

            default:
                break;
        }

        QTest::keyClick(target, Qt::Key_Tab);
    }
    qInfo("Tab order: %s", qPrintable(order.join(", ")));

    PDFDocumentWriter writer(nullptr);
    QVERIFY(writer.write(outputFileName + "-filled.pdf", fixture.document.data(), false));

    PDFDocument flattened = fixture.forms.createFlattenedDocument();
    QVERIFY(writer.write(outputFileName + "-flattened.pdf", &flattened, false));
    PDFFormFieldValueHistory::clear();
}

QTEST_MAIN(FormFieldsTest)

#include "tst_formfieldstest.moc"
