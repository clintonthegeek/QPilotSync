# QPilotSync

Modern Palm Pilot synchronization for Linux using Qt6.

## About

QPilotSync brings classic Palm Pilot devices into the 2020s with support for modern data formats and contemporary desktop environments. Sync your Palm calendar to iCalendar files, contacts to vCards, and memos to Markdown - all with a clean Qt6 interface.

## Status

🚧 **Phase 1: Foundation** (In Progress)

Current functionality:
- Basic Qt6 application structure
- Menu system and logging UI
- Build system with pilot-link integration

Coming next:
- Device connection and detection
- Reading Palm user information
- Device information display

## Features (Planned)

### Phase 1: Foundation ✓ (Current)
- Device detection and connection
- Read Palm user info
- Basic UI with sync log

### Phase 2: Read-Only Sync
- Calendar export to .ics files
- Contact export to .vcf vCards
- Memo export to Markdown files

### Phase 3: Bidirectional Sync
- Two-way synchronization
- Conflict detection and resolution
- Backup state tracking

### Phase 4-6: Polish
- File installer for .pdb/.prc applications
- Full backup and restore
- Configuration management
- Multiple device support

See `docs/ROADMAP.md` for detailed development plan.

## Build Requirements

- **Qt6** (6.2 or later)
- **KDE Frameworks 6** (KF6CalendarCore)
- **CMake** 3.19+
- **C++20** compatible compiler (GCC 10+, Clang 10+)
- **pilot-link** source (included as symlink)
- **libusb** development files
- **autoconf**, **automake**, **libtool** (for building pilot-link)

### Installing Dependencies

**Arch Linux:**
```bash
sudo pacman -S base-devel cmake qt6-base kf6-kcalendarcore libusb
```

**Ubuntu/Debian:**
```bash
sudo apt install build-essential cmake qt6-base-dev \
    libkf6calendarcore-dev libusb-dev autoconf automake libtool
```

**Fedora:**
```bash
sudo dnf install @development-tools cmake qt6-qtbase-devel \
    kf6-kcalendarcore-devel libusb-devel autoconf automake libtool
```

## Building

### 1. Prepare pilot-link

The pilot-link library is included as a symlink. First, prepare it for building:

```bash
cd pilot-link
./autogen.sh
# Or if autogen.sh doesn't exist:
autoreconf -fi
cd ..
```

### 2. Build QPilotSync

```bash
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

### 3. Run

```bash
./qpilotsync
```

## Palm Device Setup

### USB Device Permissions

To allow QPilotSync to communicate with your Palm device, create a udev rule:

```bash
sudo tee /etc/udev/rules.d/60-palm.rules << EOF
# Palm devices
SUBSYSTEM=="usb", ATTR{idVendor}=="0830", MODE="0666"
EOF
```

Reload udev rules:
```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

### Supported Devices

QPilotSync should work with:
- Palm OS 3.x devices (Palm III, V, Vx, etc.)
- Palm OS 4.x devices (m100, m500, m505, etc.)
- Palm OS 5.x devices (Tungsten, Zire, LifeDrive, etc.)
- Handspring devices (Visor, Treo)
- Sony CLIÉ devices

Both USB and serial connections are supported.

## Documentation

Comprehensive documentation is available in the `docs/` directory:

- **[PROJECT_VISION.md](docs/PROJECT_VISION.md)** - Project goals and philosophy
- **[ARCHITECTURE.md](docs/ARCHITECTURE.md)** - Technical architecture and design
- **[ROADMAP.md](docs/ROADMAP.md)** - Development phases and milestones
- **[CHALLENGES.md](docs/CHALLENGES.md)** - Known challenges and mitigation strategies
- **[FIELD_MAPPING.md](docs/FIELD_MAPPING.md)** - Palm ↔ iCalendar/vCard data mapping
- **[GETTING_STARTED.md](docs/GETTING_STARTED.md)** - Development setup guide

## Project Structure

```
QPilotSync/
├── CMakeLists.txt              # Main build configuration
├── src/                        # Source code
│   ├── main.cpp               # Application entry point
│   ├── backends/              # Backend implementations (Palm, Local)
│   ├── models/                # Data models
│   ├── ui/                    # User interface components
│   ├── sync/                  # Sync engine
│   └── palm/                  # Palm device communication
├── lib/                       # External libraries
│   └── CMakeLists.txt         # Builds pilot-link
├── docs/                      # Documentation
├── tests/                     # Test suites
└── build/                     # Build output (gitignored)
```

## Contributing

QPilotSync is in early development. Contributions are welcome!

Areas where help is needed:
- Testing with various Palm devices
- UI/UX design
- Documentation
- Package maintenance for various distributions

## License

To be determined - likely GPL-2.0 (to match KPilot and pilot-link)

## Credits

QPilotSync is built on the shoulders of giants:

- **KPilot** - The original KDE Palm sync solution (inspiration)
- **pilot-link** - The definitive library for Palm device communication
- **PlanStanLite** - Modern Qt6 calendar app (architecture patterns)
- **KDE Frameworks** - Robust calendar and contact handling

## Similar Projects

- **KPilot** - The original KDE4 Palm sync tool (no longer maintained)
- **jpilot** - GTK-based Palm sync application
- **coldsync** - Command-line Palm sync tool

## Contact

- **Project Home**: (TBD)
- **Issues**: (TBD)
- **Discussions**: (TBD)

---

**The Palm Pilot is coming back!** 🌴📱
