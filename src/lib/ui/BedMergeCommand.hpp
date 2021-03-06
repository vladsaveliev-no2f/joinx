#pragma once

#include "ui/CommandBase.hpp"
#include "common/cstdint.hpp"

#include <string>

class Bed;

class BedMergeCommand : public CommandBase {
public:
    BedMergeCommand();

    std::string name() const { return "bed-merge"; }
    std::string description() const {
        return "merge overlapping bed entries in a bed file";
    }

    void exec();
    void configureOptions();

    bool shouldMerge(Bed const& a, Bed const& b) const;

protected:
    std::string _inputFile;
    std::string _outputFile;
    size_t _distance;
    bool _names;
    bool _count;
    bool _uniqueNames;
};
