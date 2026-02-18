#pragma once

#include "iconduit.h"

class IToolConduit : public IConduit
{
public:
    // External tool path
    virtual QString toolPath() const = 0;

    // Prepare config files, working directory, etc. before execution
    virtual bool prepareExecution(Sync::SyncContext *context) = 0;

    // After tool runs: collect output files, push to installQueue
    virtual bool installResults(Sync::SyncContext *context) = 0;
};

Q_DECLARE_INTERFACE(IToolConduit, "ca.vibekoder.IToolConduit/1.0")
