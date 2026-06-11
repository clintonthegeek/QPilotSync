// src/app/patchbay/patchbaymodel.h
// Task 5 placeholder — replaced with the real model in Task 6.
#pragma once

#include <QObject>

#include "patchbaytypes.h"

namespace WildPalms::AppPatchbay {

class PatchbayModel : public QObject {
    Q_OBJECT
public:
    explicit PatchbayModel(QObject *parent = nullptr);
};

} // namespace WildPalms::AppPatchbay
