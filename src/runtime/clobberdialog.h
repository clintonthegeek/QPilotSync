#pragma once

#include <QDialog>
#include <QList>
#include <QMap>
#include <QString>

namespace WildPalms::Runtime {

/// Modal dialog presenting per-conduit checkboxes for selecting which
/// Palm-direct mappings to clobber. Has zero engine knowledge: receives
/// a domain→mapping-IDs map at construction, returns the selected
/// mapping IDs on accept.
class ClobberDialog : public QDialog {
    Q_OBJECT
public:
    /// Key: domain name (e.g. "calendar"). Value: enabled Palm-direct
    /// mapping IDs for that domain.
    using DomainMappings = QMap<QString, QList<QString>>;

    explicit ClobberDialog(const DomainMappings &mappings,
                           QWidget *parent = nullptr);
    ~ClobberDialog() override;

    /// Programmatic setter (for tests). Same as ticking the box.
    void setDomainChecked(const QString &domain, bool checked);

    /// Mapping IDs corresponding to currently-checked domains.
    /// Order matches insertion order of DomainMappings.
    QList<QString> selectedMappingIds() const;

public Q_SLOTS:
    void accept() override;  // shows the final warning prompt

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace WildPalms::Runtime
