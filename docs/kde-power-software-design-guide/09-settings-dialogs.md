# Settings Dialogs

This document covers settings dialog patterns using KPageDialog and KConfigSkeleton.

---

## KPageDialog - Multi-Page Settings

### Basic Setup

```cpp
#include <KPageDialog>
#include <KPageWidgetItem>

class SettingsDialog : public KPageDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr)
        : KPageDialog(parent)
    {
        setWindowTitle(i18n("Settings"));
        setFaceType(KPageDialog::List);  // Left sidebar

        // Standard buttons
        setStandardButtons(QDialogButtonBox::Ok |
                           QDialogButtonBox::Apply |
                           QDialogButtonBox::Cancel |
                           QDialogButtonBox::RestoreDefaults);

        // Add pages
        addGeneralPage();
        addAppearancePage();
        addAdvancedPage();

        // Connect buttons
        connect(buttonBox()->button(QDialogButtonBox::Apply),
                &QPushButton::clicked, this, &SettingsDialog::apply);
        connect(buttonBox()->button(QDialogButtonBox::RestoreDefaults),
                &QPushButton::clicked, this, &SettingsDialog::restoreDefaults);

        // Initially disable Apply
        buttonBox()->button(QDialogButtonBox::Apply)->setEnabled(false);
    }

private:
    void addGeneralPage()
    {
        m_generalPage = new GeneralSettingsPage(this);
        KPageWidgetItem *item = addPage(m_generalPage,
                                        i18nc("@title:tab", "General"));
        item->setIcon(QIcon::fromTheme(QStringLiteral("configure")));

        connect(m_generalPage, &SettingsPageBase::changed,
                this, &SettingsDialog::onSettingsChanged);
    }

    void addAppearancePage()
    {
        m_appearancePage = new AppearanceSettingsPage(this);
        KPageWidgetItem *item = addPage(m_appearancePage,
                                        i18nc("@title:tab", "Appearance"));
        item->setIcon(QIcon::fromTheme(QStringLiteral("preferences-desktop-theme")));

        connect(m_appearancePage, &SettingsPageBase::changed,
                this, &SettingsDialog::onSettingsChanged);
    }

    void addAdvancedPage()
    {
        m_advancedPage = new AdvancedSettingsPage(this);
        KPageWidgetItem *item = addPage(m_advancedPage,
                                        i18nc("@title:tab", "Advanced"));
        item->setIcon(QIcon::fromTheme(QStringLiteral("preferences-other")));

        connect(m_advancedPage, &SettingsPageBase::changed,
                this, &SettingsDialog::onSettingsChanged);
    }

private Q_SLOTS:
    void onSettingsChanged()
    {
        buttonBox()->button(QDialogButtonBox::Apply)->setEnabled(true);
    }

    void apply()
    {
        m_generalPage->applySettings();
        m_appearancePage->applySettings();
        m_advancedPage->applySettings();

        buttonBox()->button(QDialogButtonBox::Apply)->setEnabled(false);

        Q_EMIT settingsChanged();
    }

    void restoreDefaults()
    {
        // Restore defaults for current page only
        if (auto *page = qobject_cast<SettingsPageBase *>(currentPage()->widget())) {
            page->restoreDefaults();
        }
    }

    void accept() override
    {
        apply();
        KPageDialog::accept();
    }

Q_SIGNALS:
    void settingsChanged();

private:
    GeneralSettingsPage *m_generalPage;
    AppearanceSettingsPage *m_appearancePage;
    AdvancedSettingsPage *m_advancedPage;
};
```

### Face Types

```cpp
// Different dialog layouts
setFaceType(KPageDialog::List);      // Left sidebar with icons
setFaceType(KPageDialog::Tree);      // Left sidebar with tree
setFaceType(KPageDialog::Tabbed);    // Top tabs
setFaceType(KPageDialog::Plain);     // Single page (no navigation)
setFaceType(KPageDialog::FlatList);  // Flat icon list

// Adaptive for mobile
if (KRuntimePlatform::runtimePlatform().contains(QLatin1String("phone"))) {
    setFaceType(KPageDialog::Tabbed);
} else {
    setFaceType(KPageDialog::List);
}
```

---

## Settings Page Base Class

### Pattern from Dolphin

```cpp
// Based on dolphin/src/settings/settingspagebase.h

class SettingsPageBase : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPageBase(QWidget *parent = nullptr)
        : QWidget(parent)
    {}

    virtual void applySettings() = 0;
    virtual void restoreDefaults() = 0;

Q_SIGNALS:
    void changed();
};
```

### Example Settings Page

```cpp
class GeneralSettingsPage : public SettingsPageBase
{
    Q_OBJECT

public:
    explicit GeneralSettingsPage(QWidget *parent = nullptr)
        : SettingsPageBase(parent)
    {
        QVBoxLayout *layout = new QVBoxLayout(this);

        // Behavior group
        QGroupBox *behaviorGroup = new QGroupBox(i18n("Behavior"), this);
        QVBoxLayout *behaviorLayout = new QVBoxLayout(behaviorGroup);

        m_confirmDelete = new QCheckBox(i18n("Confirm before deleting files"), this);
        behaviorLayout->addWidget(m_confirmDelete);

        m_showHidden = new QCheckBox(i18n("Show hidden files"), this);
        behaviorLayout->addWidget(m_showHidden);

        layout->addWidget(behaviorGroup);

        // Startup group
        QGroupBox *startupGroup = new QGroupBox(i18n("Startup"), this);
        QFormLayout *startupLayout = new QFormLayout(startupGroup);

        m_startLocation = new KUrlRequester(this);
        m_startLocation->setMode(KFile::Directory);
        startupLayout->addRow(i18n("Start location:"), m_startLocation);

        m_restoreSession = new QCheckBox(i18n("Restore previous session"), this);
        startupLayout->addRow(QString(), m_restoreSession);

        layout->addWidget(startupGroup);
        layout->addStretch();

        // Connect change signals
        connect(m_confirmDelete, &QCheckBox::toggled,
                this, &SettingsPageBase::changed);
        connect(m_showHidden, &QCheckBox::toggled,
                this, &SettingsPageBase::changed);
        connect(m_startLocation, &KUrlRequester::textChanged,
                this, &SettingsPageBase::changed);
        connect(m_restoreSession, &QCheckBox::toggled,
                this, &SettingsPageBase::changed);

        // Load current settings
        loadSettings();
    }

    void applySettings() override
    {
        KConfigGroup config(KSharedConfig::openConfig(),
                            QStringLiteral("General"));

        config.writeEntry("ConfirmDelete", m_confirmDelete->isChecked());
        config.writeEntry("ShowHiddenFiles", m_showHidden->isChecked());
        config.writeEntry("StartLocation", m_startLocation->url());
        config.writeEntry("RestoreSession", m_restoreSession->isChecked());

        config.sync();
    }

    void restoreDefaults() override
    {
        m_confirmDelete->setChecked(true);
        m_showHidden->setChecked(false);
        m_startLocation->setUrl(QUrl::fromLocalFile(QDir::homePath()));
        m_restoreSession->setChecked(true);
    }

private:
    void loadSettings()
    {
        KConfigGroup config(KSharedConfig::openConfig(),
                            QStringLiteral("General"));

        m_confirmDelete->setChecked(config.readEntry("ConfirmDelete", true));
        m_showHidden->setChecked(config.readEntry("ShowHiddenFiles", false));
        m_startLocation->setUrl(config.readEntry("StartLocation",
            QUrl::fromLocalFile(QDir::homePath())));
        m_restoreSession->setChecked(config.readEntry("RestoreSession", true));
    }

    QCheckBox *m_confirmDelete;
    QCheckBox *m_showHidden;
    KUrlRequester *m_startLocation;
    QCheckBox *m_restoreSession;
};
```

---

## KConfigSkeleton - Declarative Settings

### Define Settings Schema

Create a `.kcfg` file:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<kcfg xmlns="http://www.kde.org/standards/kcfg/1.0"
      xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
      xsi:schemaLocation="http://www.kde.org/standards/kcfg/1.0
                          http://www.kde.org/standards/kcfg/1.0/kcfg.xsd">
  <kcfgfile name="myapprc"/>

  <group name="General">
    <entry name="ConfirmDelete" type="Bool">
      <label>Confirm before deleting files</label>
      <default>true</default>
    </entry>

    <entry name="ShowHiddenFiles" type="Bool">
      <label>Show hidden files</label>
      <default>false</default>
    </entry>

    <entry name="StartLocation" type="Url">
      <label>Location to open on startup</label>
      <default code="true">QUrl::fromLocalFile(QDir::homePath())</default>
    </entry>

    <entry name="RestoreSession" type="Bool">
      <label>Restore previous session on startup</label>
      <default>true</default>
    </entry>
  </group>

  <group name="Appearance">
    <entry name="FontFamily" type="Font">
      <label>Display font</label>
      <default code="true">QFontDatabase::systemFont(QFontDatabase::GeneralFont)</default>
    </entry>

    <entry name="IconSize" type="Int">
      <label>Icon size in pixels</label>
      <default>48</default>
      <min>16</min>
      <max>256</max>
    </entry>

    <entry name="ViewMode" type="Enum">
      <label>View mode</label>
      <choices>
        <choice name="Icons"/>
        <choice name="Compact"/>
        <choice name="Details"/>
      </choices>
      <default>Icons</default>
    </entry>

    <entry name="HighlightColor" type="Color">
      <label>Highlight color</label>
      <default>#3daee9</default>
    </entry>
  </group>

  <group name="Advanced">
    <entry name="MaxRecentFiles" type="Int">
      <label>Maximum recent files to remember</label>
      <default>20</default>
      <min>5</min>
      <max>100</max>
    </entry>

    <entry name="AutoSaveInterval" type="Int">
      <label>Auto-save interval in seconds (0 to disable)</label>
      <default>60</default>
      <min>0</min>
      <max>3600</max>
    </entry>
  </group>
</kcfg>
```

### Generate Settings Class

Create a `.kcfgc` file:

```ini
File=myappsettings.kcfg
ClassName=MyAppSettings
Mutators=true
Singleton=true
```

CMake:

```cmake
kconfig_add_kcfg_files(myapp_SRCS myappsettings.kcfgc)
```

### Using Generated Settings

```cpp
#include "myappsettings.h"

void MyWidget::loadSettings()
{
    MyAppSettings *settings = MyAppSettings::self();

    // Read settings (type-safe)
    if (settings->confirmDelete()) {
        // ...
    }

    int iconSize = settings->iconSize();
    QFont font = settings->fontFamily();
    MyAppSettings::ViewMode mode = settings->viewMode();
}

void MyWidget::saveSettings()
{
    MyAppSettings *settings = MyAppSettings::self();

    settings->setIconSize(64);
    settings->setViewMode(MyAppSettings::Details);

    // Write to disk
    settings->save();
}
```

### KConfigDialog with KConfigSkeleton

```cpp
#include <KConfigDialog>

void MainWindow::showSettings()
{
    // Check if dialog already exists
    if (KConfigDialog::showDialog(QStringLiteral("settings"))) {
        return;
    }

    // Create dialog
    KConfigDialog *dialog = new KConfigDialog(
        this,
        QStringLiteral("settings"),
        MyAppSettings::self());

    // Add pages
    GeneralPage *generalPage = new GeneralPage(dialog);
    dialog->addPage(generalPage, i18n("General"),
                    QStringLiteral("configure"));

    AppearancePage *appearancePage = new AppearancePage(dialog);
    dialog->addPage(appearancePage, i18n("Appearance"),
                    QStringLiteral("preferences-desktop-theme"));

    // Connect settings changed
    connect(dialog, &KConfigDialog::settingsChanged,
            this, &MainWindow::onSettingsChanged);

    dialog->show();
}
```

---

## Change Tracking with Validation

### Unsaved Changes Warning

```cpp
// Based on kdevelop/kdevplatform/shell/configdialog.cpp

class ConfigDialog : public KPageDialog
{
    Q_OBJECT

public:
    explicit ConfigDialog(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (m_hasUnsavedChanges) {
            int result = KMessageBox::warningTwoActionsCancel(
                this,
                i18n("You have unsaved changes. Do you want to apply them?"),
                i18n("Unsaved Changes"),
                KStandardGuiItem::apply(),
                KStandardGuiItem::discard());

            switch (result) {
            case KMessageBox::PrimaryAction:
                apply();
                break;
            case KMessageBox::SecondaryAction:
                break;  // Discard
            case KMessageBox::Cancel:
                event->ignore();
                return;
            }
        }
        event->accept();
    }

private Q_SLOTS:
    void onCurrentPageChanged(KPageWidgetItem *current,
                              KPageWidgetItem *before)
    {
        // Warn about unsaved changes when switching pages
        if (m_currentPageHasChanges && before) {
            int result = KMessageBox::warningTwoActionsCancel(
                this,
                i18n("The settings of the current page have changed.\n"
                     "Do you want to apply the changes or discard them?"),
                i18n("Apply Settings"),
                KStandardGuiItem::apply(),
                KStandardGuiItem::discard());

            if (result == KMessageBox::PrimaryAction) {
                applyCurrentPage(before);
            } else if (result == KMessageBox::SecondaryAction) {
                resetCurrentPage(before);
            } else {
                // Cancel - stay on current page
                setCurrentPage(before);
                return;
            }
        }

        m_currentPageHasChanges = false;
    }

private:
    bool m_hasUnsavedChanges = false;
    bool m_currentPageHasChanges = false;
};
```

---

## Hierarchical Settings (Sub-Pages)

```cpp
void SettingsDialog::addViewSettingsPage()
{
    // Parent page
    ViewSettingsPage *viewPage = new ViewSettingsPage(this);
    KPageWidgetItem *viewItem = addPage(viewPage, i18n("View"));
    viewItem->setIcon(QIcon::fromTheme(QStringLiteral("view-preview")));

    // Sub-pages
    IconsSettingsPage *iconsPage = new IconsSettingsPage(this);
    KPageWidgetItem *iconsItem = addSubPage(viewItem, iconsPage, i18n("Icons"));

    DetailsSettingsPage *detailsPage = new DetailsSettingsPage(this);
    KPageWidgetItem *detailsItem = addSubPage(viewItem, detailsPage, i18n("Details"));

    PreviewSettingsPage *previewPage = new PreviewSettingsPage(this);
    KPageWidgetItem *previewItem = addSubPage(viewItem, previewPage, i18n("Previews"));
}
```

---

## Plugin Configuration Pages

### KTextEditor::ConfigPage Interface

```cpp
#include <KTextEditor/ConfigPage>

class MyPluginConfigPage : public KTextEditor::ConfigPage
{
    Q_OBJECT

public:
    explicit MyPluginConfigPage(QWidget *parent, MyPlugin *plugin)
        : KTextEditor::ConfigPage(parent)
        , m_plugin(plugin)
    {
        setupUi();
        reset();  // Load current values
    }

    // Required interface
    QString name() const override
    {
        return i18n("My Plugin");
    }

    QString fullName() const override
    {
        return i18n("My Plugin Settings");
    }

    QIcon icon() const override
    {
        return QIcon::fromTheme(QStringLiteral("preferences-plugin"));
    }

    void apply() override
    {
        KConfigGroup config(KSharedConfig::openConfig(),
                            QStringLiteral("MyPlugin"));
        config.writeEntry("Option1", m_option1->isChecked());
        config.writeEntry("Option2", m_option2->value());
        config.sync();

        m_plugin->configChanged();
    }

    void reset() override
    {
        KConfigGroup config(KSharedConfig::openConfig(),
                            QStringLiteral("MyPlugin"));
        m_option1->setChecked(config.readEntry("Option1", true));
        m_option2->setValue(config.readEntry("Option2", 42));
    }

    void defaults() override
    {
        m_option1->setChecked(true);
        m_option2->setValue(42);
    }

private:
    void setupUi()
    {
        QVBoxLayout *layout = new QVBoxLayout(this);

        m_option1 = new QCheckBox(i18n("Enable Option 1"), this);
        layout->addWidget(m_option1);

        QHBoxLayout *option2Layout = new QHBoxLayout();
        option2Layout->addWidget(new QLabel(i18n("Option 2:"), this));
        m_option2 = new QSpinBox(this);
        m_option2->setRange(1, 100);
        option2Layout->addWidget(m_option2);
        option2Layout->addStretch();
        layout->addLayout(option2Layout);

        layout->addStretch();

        // Connect changed signal
        connect(m_option1, &QCheckBox::toggled,
                this, &MyPluginConfigPage::changed);
        connect(m_option2, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &MyPluginConfigPage::changed);
    }

    MyPlugin *m_plugin;
    QCheckBox *m_option1;
    QSpinBox *m_option2;
};
```

### Registering Plugin Config Pages

```cpp
// In plugin class
int MyPlugin::configPages() const
{
    return 1;  // Number of config pages
}

KTextEditor::ConfigPage *MyPlugin::configPage(int number, QWidget *parent)
{
    if (number == 0) {
        return new MyPluginConfigPage(parent, this);
    }
    return nullptr;
}
```

---

## Best Practices

### 1. Immediate Feedback

```cpp
// Preview changes in real-time where appropriate
connect(m_fontCombo, &QFontComboBox::currentFontChanged,
        this, [this](const QFont &font) {
    // Update preview immediately
    m_previewLabel->setFont(font);
    // Mark as changed
    Q_EMIT changed();
});
```

### 2. Sensible Defaults

```cpp
void SettingsPage::restoreDefaults()
{
    // Use values that work for most users
    m_iconSize->setValue(48);  // Not too big, not too small
    m_confirmDelete->setChecked(true);  // Safer default
    m_showHidden->setChecked(false);  // Cleaner view
}
```

### 3. Validation

```cpp
void SettingsPage::validateInput()
{
    bool valid = true;
    QString error;

    // Check directory exists
    if (!QDir(m_startLocation->url().toLocalFile()).exists()) {
        valid = false;
        error = i18n("Start location does not exist");
    }

    // Check value ranges
    if (m_autoSave->value() > 0 && m_autoSave->value() < 10) {
        valid = false;
        error = i18n("Auto-save interval must be at least 10 seconds");
    }

    m_errorLabel->setText(error);
    m_errorLabel->setVisible(!valid);

    Q_EMIT validityChanged(valid);
}
```

### 4. Grouping Related Settings

```cpp
void SettingsPage::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);

    // Group 1: Behavior
    QGroupBox *behaviorGroup = new QGroupBox(i18n("Behavior"), this);
    // ... add behavior widgets ...
    layout->addWidget(behaviorGroup);

    // Group 2: Appearance
    QGroupBox *appearanceGroup = new QGroupBox(i18n("Appearance"), this);
    // ... add appearance widgets ...
    layout->addWidget(appearanceGroup);

    // Use stretch to push groups to top
    layout->addStretch();
}
```

### 5. Help Text

```cpp
// Add tooltips for complex options
m_autoSave->setToolTip(
    i18n("How often to automatically save documents.\n"
         "Set to 0 to disable auto-save."));

// Use What's This for detailed explanations
m_advancedOption->setWhatsThis(
    i18n("This option enables advanced functionality that may "
         "affect performance. Only enable if you understand "
         "the implications."));
```
