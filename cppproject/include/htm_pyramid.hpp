#pragma once

// HTM Pyramid - hierarchical structure
// Matches Python's ModelPyramid
// L0: 16 modules (feature groups)
// L1: 6 modules (merge L0)
// L2: 3 modules (merge L1)
// L3: 1 module (final head)

#include <string>
#include <vector>
#include <map>
#include <memory>

// Forward declare
namespace htm_swat {
    class HTMModule;
}

namespace htm_swat {

class HTMPyramid {
public:
    HTMPyramid(int seed = 69, int learn_period = 1000);
    ~HTMPyramid();
    
    // TODO: Set up pyramid - matches Python initialize()
    void initialize(const std::map<std::string, std::vector<std::string>>& features,
                    const std::map<std::string, std::vector<std::string>>& connections);
    
    // TODO: Process one data row - matches Python processRow()
    // float processRow(const std::map<std::string, float>& row_data);
    
    // TODO: Process whole dataset
    // std::vector<float> processData(const std::vector<std::map<std::string, float>>& data);
    
    // TODO: Get final anomaly score
    // float getHeadAnomalyScore() const;
    
private:
    void buildPyramid();
    
    // TODO: Merge SDRs using union (Python uses mode='u')
    // htm::SDR mergeSDRs(const std::vector<htm::SDR>& sdrs, const std::string& mode);
    
    // TODO: Encode a feature group
    // htm::SDR encodeFeatureGroup(const std::string& group_name, 
    //                              const std::map<std::string, float>& row_data);
    
    // All the HTM modules
    std::map<std::string, std::unique_ptr<HTMModule>> modules_;
    
    // Settings
    int seed_;
    int learn_period_;
    std::string htm_merge_mode_;  // "u" = union
    
    // Feature and connection definitions
    std::map<std::string, std::vector<std::string>> features_;
    std::map<std::string, std::vector<std::string>> connections_;
};

} // namespace htm_swat

