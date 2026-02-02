#!/bin/bash
# Script to convert string literals to QStringLiteral for KDE strict mode
# Run from the project root directory

set -e

SRC_DIR="${1:-src}"

echo "Converting string literals in $SRC_DIR..."

# Pattern 1: QString("...") → QStringLiteral("...")
# This handles most direct QString constructions
find "$SRC_DIR" -name "*.cpp" -exec sed -i \
    -e 's/QString("\([^"]*\)")/QStringLiteral("\1")/g' \
    {} \;

echo "✓ Converted QString(\"...\") patterns"

# Pattern 2: .arg("...") → .arg(QStringLiteral("..."))
find "$SRC_DIR" -name "*.cpp" -exec sed -i \
    -e 's/\.arg("\([^"]*\)")/.arg(QStringLiteral("\1"))/g' \
    {} \;

echo "✓ Converted .arg(\"...\") patterns"

# Pattern 3: Common function calls that take QString
# setWindowTitle, setText, setToolTip, setStatusTip, setWhatsThis
for func in setWindowTitle setText setToolTip setStatusTip setWhatsThis setPlaceholderText setLabelText showMessage; do
    find "$SRC_DIR" -name "*.cpp" -exec sed -i \
        -e "s/${func}(\"\\([^\"]*\\)\")/${func}(QStringLiteral(\"\\1\"))/g" \
        {} \;
done

echo "✓ Converted common setter functions"

# Pattern 4: addItem("...") → addItem(QStringLiteral("..."))
find "$SRC_DIR" -name "*.cpp" -exec sed -i \
    -e 's/addItem("\([^"]*\)")/addItem(QStringLiteral("\1"))/g' \
    {} \;

echo "✓ Converted addItem(\"...\") patterns"

# Pattern 5: addRow("...", → addRow(QStringLiteral("..."),
find "$SRC_DIR" -name "*.cpp" -exec sed -i \
    -e 's/addRow("\([^"]*\)",/addRow(QStringLiteral("\1"),/g' \
    {} \;

echo "✓ Converted addRow(\"...\") patterns"

# Pattern 6: startsWith("...") and endsWith("...")
find "$SRC_DIR" -name "*.cpp" -exec sed -i \
    -e 's/startsWith("\([^"]*\)")/startsWith(QStringLiteral("\1"))/g' \
    -e 's/endsWith("\([^"]*\)")/endsWith(QStringLiteral("\1"))/g' \
    {} \;

echo "✓ Converted startsWith/endsWith patterns"

# Pattern 7: contains("...")
find "$SRC_DIR" -name "*.cpp" -exec sed -i \
    -e 's/contains("\([^"]*\)")/contains(QStringLiteral("\1"))/g' \
    {} \;

echo "✓ Converted contains(\"...\") patterns"

# Pattern 8: == "..." and != "..."
find "$SRC_DIR" -name "*.cpp" -exec sed -i \
    -e 's/== "\([^"]*\)"/== QStringLiteral("\1")/g' \
    -e 's/!= "\([^"]*\)"/!= QStringLiteral("\1")/g' \
    {} \;

echo "✓ Converted == and != string comparisons"

# Pattern 9: indexOf(':') → indexOf(QLatin1Char(':'))
# Single character searches
find "$SRC_DIR" -name "*.cpp" -exec sed -i \
    -e "s/indexOf('\\([^']\\)')/indexOf(QLatin1Char('\\1'))/g" \
    -e "s/lastIndexOf('\\([^']\\)')/lastIndexOf(QLatin1Char('\\1'))/g" \
    {} \;

echo "✓ Converted single-char indexOf patterns"

# Pattern 10: split("x") for single chars → split(QLatin1Char('x'))
# Only for single character splits
find "$SRC_DIR" -name "*.cpp" -exec sed -i \
    -e 's/split("\\([^"]\\)")/split(QLatin1Char(\x27\1\x27))/g' \
    {} \;

echo "✓ Converted single-char split patterns"

# Pattern 11: logInfo/logWarning/logError/logMessage with bare strings
for func in logInfo logWarning logError logMessage; do
    find "$SRC_DIR" -name "*.cpp" -exec sed -i \
        -e "s/${func}(\"\\([^\"]*\\)\")/${func}(QStringLiteral(\"\\1\"))/g" \
        {} \;
done

echo "✓ Converted logging functions"

# Pattern 12: QDir separators - "/" in path operations
# These need QLatin1String for paths
find "$SRC_DIR" -name "*.cpp" -exec sed -i \
    -e 's/+ "\/"/+ QLatin1String("\/")/g' \
    -e 's/"\/\([^"]*\)" +/QLatin1String("\/\1") +/g' \
    {} \;

echo "✓ Converted path separator patterns"

echo ""
echo "Conversion complete!"
echo "Note: Some patterns may need manual review:"
echo "  - String concatenation with + operator"
echo "  - Multi-line strings"
echo "  - Strings that should use i18n() for translation"
echo ""
echo "Run 'make' to check for remaining issues."
