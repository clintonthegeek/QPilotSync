# Getting Started with QPilotSync Development

## Immediate Next Steps

This guide outlines exactly what to do first to get QPilotSync off the ground.

## Step 1: Create Project Structure

```bash
cd /mnt/oldhome/clinton/Sync/QPilotSync

# Create directory structure
mkdir -p src/{backends,models,ui,sync,palm}
mkdir -p lib
mkdir -p tests
mkdir -p build
mkdir -p scripts

# Create placeholder files
touch src/main.cpp
touch src/CMakeLists.txt
touch lib/CMakeLists.txt
touch CMakeLists.txt
touch README.md
```

## Step 2: Write Root CMakeLists.txt

Based on PlanStanLite's structure, create a root build configuration:

**File: `CMakeLists.txt`**
```cmake
cmake_minimum_required(VERSION 3.19)
project(QPilotSync VERSION 0.1.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6 Configuration
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Find Qt6
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets)
qt_standard_project_setup()

# Find KDE Frameworks
find_package(KF6CalendarCore REQUIRED)

# pilot-link will be built in lib/
add_subdirectory(lib)

# Main source
add_subdirectory(src)

# Tests (optional for now)
# add_subdirectory(tests)

# Main executable
qt_add_executable(qpilotsync
    WIN32 MACOSX_BUNDLE
    src/main.cpp
)

target_link_libraries(qpilotsync
    PRIVATE
        Qt::Core
        Qt::Widgets
        KF6::CalendarCore
        QPilotCore
        pisock  # pilot-link library
)

# Install target
install(TARGETS qpilotsync
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
```

## Step 3: Build pilot-link

**File: `lib/CMakeLists.txt`**
```cmake
include(ExternalProject)

set(PILOT_LINK_SOURCE_DIR ${CMAKE_SOURCE_DIR}/../pilot-link)
set(PILOT_LINK_INSTALL_DIR ${CMAKE_BINARY_DIR}/pilot-link-install)

ExternalProject_Add(pilot-link
    SOURCE_DIR ${PILOT_LINK_SOURCE_DIR}
    CONFIGURE_COMMAND ${PILOT_LINK_SOURCE_DIR}/configure
        --prefix=${PILOT_LINK_INSTALL_DIR}
        --enable-libusb
        --disable-shared
        --enable-static
    BUILD_COMMAND make
    INSTALL_COMMAND make install
    BUILD_IN_SOURCE 0
)

# Create imported library target
add_library(pisock STATIC IMPORTED GLOBAL)
set_target_properties(pisock PROPERTIES
    IMPORTED_LOCATION ${PILOT_LINK_INSTALL_DIR}/lib/libpisock.a
)

# Make headers available
include_directories(${PILOT_LINK_INSTALL_DIR}/include)

# Ensure pilot-link is built before anything else
add_dependencies(pisock pilot-link)
```

**Note**: You may need to run `autoreconf -i` in pilot-link directory first if `configure` doesn't exist.

## Step 4: Create Minimal Qt6 Application

**File: `src/main.cpp`**
```cpp
#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QTextEdit>
#include <QMessageBox>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("QPilotSync");
        setMinimumSize(800, 600);

        // Central widget - log viewer for now
        auto *logView = new QTextEdit(this);
        logView->setReadOnly(true);
        logView->append("QPilotSync initialized");
        setCentralWidget(logView);

        // Menu bar
        auto *fileMenu = menuBar()->addMenu("&File");
        auto *syncMenu = menuBar()->addMenu("&Sync");
        auto *helpMenu = menuBar()->addMenu("&Help");

        // File menu
        auto *quitAction = fileMenu->addAction("&Quit");
        connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

        // Sync menu
        auto *connectAction = syncMenu->addAction("&Connect to Device");
        connect(connectAction, &QAction::triggered, this, &MainWindow::onConnect);

        // Help menu
        auto *aboutAction = helpMenu->addAction("&About");
        connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);

        // Status bar
        statusBar()->showMessage("Ready");
    }

private slots:
    void onConnect() {
        statusBar()->showMessage("Connecting to Palm device...");
        // TODO: Implement device connection
        QMessageBox::information(this, "Connect",
            "Device connection not yet implemented");
    }

    void onAbout() {
        QMessageBox::about(this, "About QPilotSync",
            "QPilotSync 0.1.0\n\n"
            "Modern Palm Pilot synchronization for Linux\n\n"
            "Built with Qt6 and pilot-link");
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("QPilotSync");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("QPilotSync");

    MainWindow window;
    window.show();

    return app.exec();
}

#include "main.moc"
```

**File: `src/CMakeLists.txt`**
```cmake
# QPilotCore library (for future use)
add_library(QPilotCore STATIC
    # Will add source files here as we create them
)

target_link_libraries(QPilotCore
    PUBLIC
        Qt::Core
        Qt::Widgets
        KF6::CalendarCore
        pisock
)

target_include_directories(QPilotCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
```

## Step 5: Create README

**File: `README.md`**
```markdown
# QPilotSync

Modern Palm Pilot synchronization for Linux using Qt6.

## Features (Planned)

- Bidirectional sync of Calendar (to .ics files)
- Bidirectional sync of Contacts (to .vcf vCards)
- File installer for .pdb/.prc applications
- Backup and restore
- Support for Palm OS 3.x - 5.x

## Status

🚧 **Early Development** - Not yet functional

## Build Requirements

- Qt6 (6.2 or later)
- KDE Frameworks 6 (KF6CalendarCore)
- CMake 3.19+
- C++20 compiler
- pilot-link source (included)
- libusb development files

## Building

### 1. Install Dependencies (Ubuntu/Debian)

```bash
sudo apt install build-essential cmake qt6-base-dev \
    libkf6calendarcore-dev libusb-dev autoconf automake libtool
```

### 2. Prepare pilot-link

```bash
cd ../pilot-link
autoreconf -i
```

### 3. Build QPilotSync

```bash
mkdir build
cd build
cmake ..
make
```

### 4. Run

```bash
./qpilotsync
```

## Palm Device Setup

### USB Device Permissions

Create udev rule for Palm devices:

```bash
sudo nano /etc/udev/rules.d/60-palm.rules
```

Add:
```
SUBSYSTEM=="usb", ATTR{idVendor}=="0830", MODE="0666"
```

Reload udev:
```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

## Documentation

See `docs/` directory for:
- `PROJECT_VISION.md` - Project goals and philosophy
- `ARCHITECTURE.md` - Technical architecture
- `ROADMAP.md` - Development plan
- `CHALLENGES.md` - Known challenges and mitigation

## License

TBD - Suggest GPL-2.0 (to match KPilot and pilot-link)

## Credits

- Inspired by KDE's KPilot project
- Uses pilot-link library
- Architecture patterns from PlanStanLite
```

## Step 6: Initial Build Test

```bash
cd /mnt/oldhome/clinton/Sync/QPilotSync

# Prepare pilot-link (if not done)
cd ../pilot-link
autoreconf -i
cd ../QPilotSync

# Configure and build
mkdir -p build
cd build
cmake ..
make
```

**Expected result**: Should compile successfully and produce `qpilotsync` executable.

## Step 7: Test pilot-link Integration

Create a simple test to verify pilot-link is linked correctly:

**File: `tests/test_pilot_link.cpp`**
```cpp
#include <iostream>
#include <pi-version.h>

int main() {
    std::cout << "pilot-link version: " << PILOT_LINK_VERSION << std::endl;
    std::cout << "pilot-link compiled successfully!" << std::endl;
    return 0;
}
```

Compile and run:
```bash
g++ test_pilot_link.cpp -I../build/pilot-link-install/include \
    -L../build/pilot-link-install/lib -lpisock -o test_pilot_link
./test_pilot_link
```

## Step 8: Version Control Setup

```bash
# Initialize git if not done
git init

# Create .gitignore
cat > .gitignore << 'EOF'
# Build directories
build/
cmake-build-*/
*.o
*.a
*.so
*.dylib
*.exe

# IDE files
.vscode/
.idea/
*.user
*.autosave

# Qt generated
moc_*
ui_*
qrc_*

# CMake generated
CMakeCache.txt
CMakeFiles/
Makefile
cmake_install.cmake
install_manifest.txt

# Backup files
*~
*.swp
*.swo

# OS files
.DS_Store
Thumbs.db
EOF

# Initial commit
git add .
git commit -m "Initial project structure"
```

## Common Build Issues

### pilot-link configure fails
```bash
cd ../pilot-link
./autogen.sh  # Instead of autoreconf
```

### Missing libusb
```bash
sudo apt install libusb-dev  # Debian/Ubuntu
sudo pacman -S libusb        # Arch
```

### Qt6 not found
```bash
# Ensure Qt6 is installed
sudo apt install qt6-base-dev

# Or specify Qt6 path
cmake -DCMAKE_PREFIX_PATH=/path/to/qt6 ..
```

### KF6CalendarCore not found
```bash
sudo apt install libkf6calendarcore-dev  # Debian/Ubuntu
# OR build from source if not in repos
```

## Next Steps After Initial Build

Once the basic application compiles and runs:

1. **Implement KPilotLink Interface** (`src/palm/kpilotlink.h`)
2. **Create Device Connection Logic** (`src/palm/devicelink.cpp`)
3. **Add Logging System** (`src/ui/logwidget.cpp`)
4. **Test Device Detection** (connect real Palm, press HotSync)
5. **Read User Info** (display in UI)

See `ROADMAP.md` Phase 1 for detailed task list.

## Getting Help

- Read the docs in `docs/`
- Check pilot-link documentation in `../pilot-link/doc/`
- Study PlanStanLite source in `../PlanStanLite/`
- Reference KPilot source in `../kdepim-4.3.4/kpilot/`

## Development Tools

Recommended:
- **IDE**: Qt Creator, VS Code with C++ extensions, or CLion
- **Debugger**: GDB with Qt pretty-printers
- **Profiler**: Valgrind for memory leaks
- **GUI Designer**: Qt Designer (for .ui files)

## Tips

1. **Incremental Development**: Build small, test often
2. **Read Before Writing**: Study the reference projects thoroughly
3. **Test Without Device**: Use KPilotLocalLink for most development
4. **Backup First**: Always backup Palm data before testing writes
5. **Log Everything**: Comprehensive logging saves debugging time

---

Ready to start coding! 🚀
