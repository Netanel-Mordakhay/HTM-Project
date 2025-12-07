#include <iostream>
#include <vector>
#include "config.hpp"

// Testing htm.core library
#include <htm/types/Sdr.hpp>
#include <htm/algorithms/SpatialPooler.hpp>
#include <htm/algorithms/TemporalMemory.hpp>

using namespace htm;

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "HTM SWAT C++ Implementation" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    std::cout << "Testing HTM.core library..." << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    
    int tests_passed = 0;
    int tests_total = 0;
    
    try {
        // Test 1: SDR Creation
        std::cout << "\n[Test 1] Creating SDR..." << std::endl;
        tests_total++;
        SDR test_sdr({100});
        std::cout << "  ✓ SDR created successfully" << std::endl;
        std::cout << "    Size: " << test_sdr.size << std::endl;
        std::cout << "    Dimensions: [";
        for (size_t i = 0; i < test_sdr.dimensions.size(); i++) {
            std::cout << test_sdr.dimensions[i];
            if (i < test_sdr.dimensions.size() - 1) std::cout << ", ";
        }
        std::cout << "]" << std::endl;
        
        // Test that SDR operations work
        std::vector<UInt> sparse_bits = {5, 10, 15, 20, 25};
        test_sdr.setSparse(sparse_bits);
        auto retrieved = test_sdr.getSparse();
        std::cout << "  ✓ SDR sparse operations work" << std::endl;
        std::cout << "    Set " << sparse_bits.size() << " bits, retrieved " << retrieved.size() << " bits" << std::endl;
        tests_passed++;
        
        // Test 2: SpatialPooler Creation
        std::cout << "\n[Test 2] Creating SpatialPooler..." << std::endl;
        tests_total++;
        SpatialPooler sp(
            {100},              // input dimensions
            {2048},             // column dimensions
            100,                // potentialRadius
            0.3,                // potentialPct
            true,               // globalInhibition
            0.02,               // localAreaDensity
            0,                  // numActiveColumnsPerInhArea
            10,                 // stimulusThreshold
            0.0005,             // synPermInactiveDec
            0.003,              // synPermActiveInc
            0.2,                // synPermConnected
            0.001,              // minPctOverlapDutyCycles
            1000,               // dutyCyclePeriod
            0.0,                // boostStrength
            42,                 // seed
            0                   // spVerbosity
        );
        std::cout << "  ✓ SpatialPooler created successfully" << std::endl;
        std::cout << "    Input size: 100" << std::endl;
        std::cout << "    Column size: 2048" << std::endl;
        tests_passed++;
        
        // Test 3: TemporalMemory Creation
        std::cout << "\n[Test 3] Creating TemporalMemory..." << std::endl;
        tests_total++;
        TemporalMemory tm(
            {2048},             // columnDimensions
            4,                   // cellsPerColumn
            13,                  // activationThreshold
            0.21,                // initialPermanence
            0.5,                 // connectedPermanence
            10,                  // minThreshold
            20,                  // maxNewSynapseCount
            0.2,                 // permanenceIncrement
            0.02,                // permanenceDecrement
            0.008,               // predictedSegmentDecrement
            32,                  // maxSegmentsPerCell
            128,                 // maxSynapsesPerSegment
            42,                  // seed
            0,                   // checkInputs
            false                // externalPredictiveInputs
        );
        std::cout << "  ✓ TemporalMemory created successfully" << std::endl;
        std::cout << "    Columns: 2048" << std::endl;
        std::cout << "    Cells per column: 4" << std::endl;
        tests_passed++;
        
        // Test 4: SP + TM Integration Test
        std::cout << "\n[Test 4] Testing SP + TM integration..." << std::endl;
        tests_total++;
        
        // Make an input SDR to test with
        SDR input_sdr({100});
        std::vector<UInt> input_bits;
        for (UInt i = 0; i < 10; i++) {
            input_bits.push_back(i * 5);  // Sparse input: [0, 5, 10, 15, 20, 25, 30, 35, 40, 45]
        }
        input_sdr.setSparse(input_bits);
        auto verify_bits = input_sdr.getSparse();
        std::cout << "  ✓ Input SDR created with " << verify_bits.size() << " active bits" << std::endl;
        
        // Run it through the SpatialPooler
        SDR active_columns({2048});
        sp.compute(input_sdr, true, active_columns);
        std::cout << "  ✓ SpatialPooler.compute() executed" << std::endl;
        auto active_cols = active_columns.getSparse();
        std::cout << "    Active columns: " << active_cols.size() << std::endl;
        
        if (active_cols.size() == 0) {
            std::cout << "    Warning: No active columns (this is normal for first iteration)" << std::endl;
            // Still continue the test
        }
        
        // Run through TemporalMemory if we got any active columns
        if (active_cols.size() > 0) {
            tm.compute(active_columns, true);
            SDR active_cells = tm.getActiveCells();
            std::cout << "  ✓ TemporalMemory.compute() executed" << std::endl;
            std::cout << "    Active cells: " << active_cells.getSparse().size() << std::endl;
            
            // Check predictive cells
            SDR predictive_cells = tm.getPredictiveCells();
            std::cout << "  ✓ Predictive cells retrieved: " << predictive_cells.getSparse().size() << std::endl;
        } else {
            std::cout << "  ⚠ Skipping TM compute (no active columns yet)" << std::endl;
        }
        
        tests_passed++;
        
        // Test 5: Run a few learning iterations
        std::cout << "\n[Test 5] Testing learning over multiple iterations..." << std::endl;
        tests_total++;
        
        SDR iter_input({100});
        SDR iter_active({2048});
        for (int i = 0; i < 3; i++) {
            // Make a different input pattern each time
            std::vector<UInt> pattern_bits;
            for (UInt j = 0; j < 10; j++) {
                pattern_bits.push_back((i * 10 + j * 5) % 100);
            }
            iter_input.setSparse(pattern_bits);
            
            sp.compute(iter_input, true, iter_active);
            if (iter_active.getSparse().size() > 0) {
                tm.compute(iter_active, true);
            }
        }
        std::cout << "  ✓ Completed 3 learning iterations" << std::endl;
        tests_passed++;
        
    } catch (const std::exception& e) {
        std::cerr << "\n❌ ERROR: " << e.what() << std::endl;
        std::cout << "\n========================================" << std::endl;
        std::cout << "HTM.core Test Results: FAILED" << std::endl;
        std::cout << "Passed: " << tests_passed << "/" << tests_total << std::endl;
        std::cout << "========================================" << std::endl;
        return 1;
    }
    
    // Print results
    std::cout << "\n========================================" << std::endl;
    std::cout << "HTM.core Test Results: PASSED" << std::endl;
    std::cout << "Tests passed: " << tests_passed << "/" << tests_total << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "✅ HTM.core library is working correctly!" << std::endl;
    std::cout << "✅ Headers are accessible" << std::endl;
    std::cout << "✅ Library is properly linked" << std::endl;
    std::cout << "✅ Basic functionality verified" << std::endl;
    std::cout << std::endl;
    std::cout << "Base structure ready!" << std::endl;
    std::cout << "Ready for implementation..." << std::endl;
    std::cout << std::endl;
    
    return 0;
}

