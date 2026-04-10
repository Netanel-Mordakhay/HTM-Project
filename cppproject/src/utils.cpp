#include "utils.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <iostream>
#include <iomanip>

// Apache Arrow includes for Parquet support
#ifdef USE_ARROW
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>
#include <parquet/stream_reader.h>
#include <parquet/stream_writer.h>
#endif

namespace htm_swat {

using namespace htm;

SDR mergeSDRsUnion(const std::vector<SDR>& sdrs) {
    if (sdrs.empty()) {
        throw std::runtime_error("Cannot merge empty SDR vector");
    }
    
    if (sdrs.size() == 1) {
        return sdrs[0];
    }
    
    // All SDRs must have the same size for union
    UInt size = sdrs[0].size;
    for (const auto& sdr : sdrs) {
        if (sdr.size != size) {
            throw std::runtime_error("All SDRs must have the same size for union");
        }
    }
    
    // Collect all active bits
    std::set<UInt> all_bits;
    for (const auto& sdr : sdrs) {
        auto sparse = sdr.getSparse();
        for (UInt bit : sparse) {
            all_bits.insert(bit);
        }
    }
    
    // Create result SDR
    SDR result({size});
    std::vector<UInt> result_bits(all_bits.begin(), all_bits.end());
    result.setSparse(result_bits);
    
    return result;
}

SDR mergeSDRsConcat(const std::vector<SDR>& sdrs) {
    if (sdrs.empty()) {
        throw std::runtime_error("Cannot concatenate empty SDR vector");
    }
    
    if (sdrs.size() == 1) {
        return sdrs[0];
    }
    
    // Calculate total size
    UInt total_size = 0;
    for (const auto& sdr : sdrs) {
        total_size += sdr.size;
    }
    
    // Concatenate bits with offsets
    std::vector<UInt> result_bits;
    UInt offset = 0;
    
    for (const auto& sdr : sdrs) {
        auto sparse = sdr.getSparse();
        for (UInt bit : sparse) {
            result_bits.push_back(bit + offset);
        }
        offset += sdr.size;
    }
    
    // Sort and remove duplicates (shouldn't happen, but safe)
    std::sort(result_bits.begin(), result_bits.end());
    result_bits.erase(std::unique(result_bits.begin(), result_bits.end()), result_bits.end());
    
    // Create result SDR
    SDR result({total_size});
    result.setSparse(result_bits);
    
    return result;
}

SDR mergeSDRs(const std::vector<SDR>& sdrs, const std::string& mode) {
    if (mode == "u" || mode == "union") {
        return mergeSDRsUnion(sdrs);
    } else if (mode == "c" || mode == "concat" || mode == "concatenate") {
        return mergeSDRsConcat(sdrs);
    } else {
        throw std::runtime_error("Unknown merge mode: " + mode);
    }
}

float calcAnomalyScore(const SDR& active, const SDR& predictive) {
    if (active.size != predictive.size) {
        throw std::runtime_error("SDRs must have the same size for anomaly calculation");
    }
    
    auto active_bits = active.getSparse();
    auto predictive_bits = predictive.getSparse();
    
    // Match Python implementation in `htm_source.utils.metric.calc_anomaly_score`:
    // If there are no active bits, return 0.0 (not anomalous),
    // because there is no evidence to compare against predictions.
    if (active_bits.empty()) {
        return 0.0f;
    }
    
    // Calculate intersection
    std::set<UInt> active_set(active_bits.begin(), active_bits.end());
    std::set<UInt> predictive_set(predictive_bits.begin(), predictive_bits.end());
    
    size_t intersection = 0;
    for (UInt bit : active_set) {
        if (predictive_set.find(bit) != predictive_set.end()) {
            intersection++;
        }
    }
    
    // Formula: 1 - (intersection_size / active_size)
    float score = 1.0f - (static_cast<float>(intersection) / static_cast<float>(active_bits.size()));
    return score;
}

std::vector<std::map<std::string, double>> loadCSV(const std::string& filename) {
    std::vector<std::map<std::string, double>> data;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filename);
    }
    
    std::string line;
    std::vector<std::string> headers;
    
    // Read header
    if (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string header;
        while (std::getline(ss, header, ',')) {
            // Remove whitespace
            header.erase(0, header.find_first_not_of(" \t\r\n"));
            header.erase(header.find_last_not_of(" \t\r\n") + 1);
            headers.push_back(header);
        }
    }
    
    // Read data rows
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::map<std::string, double> row;
        std::stringstream ss(line);
        std::string value;
        size_t col_idx = 0;
        
        while (std::getline(ss, value, ',') && col_idx < headers.size()) {
            // Remove whitespace
            value.erase(0, value.find_first_not_of(" \t\r\n"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);
            
            try {
                double num_value = std::stod(value);
                row[headers[col_idx]] = num_value;
            } catch (const std::exception& e) {
                // Skip non-numeric values or use 0.0
                row[headers[col_idx]] = 0.0;
            }
            
            col_idx++;
        }
        
        if (!row.empty()) {
            data.push_back(row);
        }
    }
    
    return data;
}

std::vector<std::map<std::string, double>> loadParquet(const std::string& filename) {
    std::vector<std::map<std::string, double>> data;
    
#ifdef USE_ARROW
    try {
        // Open parquet file
        auto infile_result = arrow::io::ReadableFile::Open(filename, arrow::default_memory_pool());
        if (!infile_result.ok()) {
            throw std::runtime_error("Failed to open parquet file: " + infile_result.status().ToString());
        }
        std::shared_ptr<arrow::io::ReadableFile> infile = *infile_result;
        
        // Create parquet reader
        auto reader_result = parquet::arrow::OpenFile(infile, arrow::default_memory_pool());
        if (!reader_result.ok()) {
            throw std::runtime_error("Failed to create parquet reader: " + reader_result.status().ToString());
        }
        std::unique_ptr<parquet::arrow::FileReader> reader = std::move(*reader_result);
        
        // Read entire file as a table
        std::shared_ptr<arrow::Table> table;
        arrow::Status status = reader->ReadTable(&table);
        if (!status.ok()) {
            throw std::runtime_error("Failed to read parquet table: " + status.ToString());
        }
        
        // Get column names
        std::vector<std::string> column_names;
        for (int i = 0; i < table->num_columns(); i++) {
            column_names.push_back(table->schema()->field(i)->name());
        }
        
        std::cout << "  Found " << table->num_columns() << " columns, " 
                  << table->num_rows() << " rows" << std::endl;
        
        // Convert to vector of maps
        for (int64_t row = 0; row < table->num_rows(); row++) {
            std::map<std::string, double> row_data;
            
            for (int col = 0; col < table->num_columns(); col++) {
                auto column = table->column(col);
                
                // Handle multiple chunks
                int64_t offset = 0;
                int chunk_idx = 0;
                for (int c = 0; c < column->num_chunks(); c++) {
                    if (row < offset + column->chunk(c)->length()) {
                        chunk_idx = c;
                        break;
                    }
                    offset += column->chunk(c)->length();
                }
                
                auto chunk = column->chunk(chunk_idx);
                int64_t local_row = row - offset;
                
                std::string col_name = column_names[col];
                double value = 0.0;
                
                // Handle different data types
                switch (column->type()->id()) {
                    case arrow::Type::DOUBLE: {
                        auto double_array = std::static_pointer_cast<arrow::DoubleArray>(chunk);
                        if (!double_array->IsNull(local_row)) {
                            value = double_array->Value(local_row);
                        }
                        break;
                    }
                    case arrow::Type::FLOAT: {
                        auto float_array = std::static_pointer_cast<arrow::FloatArray>(chunk);
                        if (!float_array->IsNull(local_row)) {
                            value = static_cast<double>(float_array->Value(local_row));
                        }
                        break;
                    }
                    case arrow::Type::INT64: {
                        auto int_array = std::static_pointer_cast<arrow::Int64Array>(chunk);
                        if (!int_array->IsNull(local_row)) {
                            value = static_cast<double>(int_array->Value(local_row));
                        }
                        break;
                    }
                    case arrow::Type::INT32: {
                        auto int_array = std::static_pointer_cast<arrow::Int32Array>(chunk);
                        if (!int_array->IsNull(local_row)) {
                            value = static_cast<double>(int_array->Value(local_row));
                        }
                        break;
                    }
                    case arrow::Type::BOOL: {
                        auto bool_array = std::static_pointer_cast<arrow::BooleanArray>(chunk);
                        if (!bool_array->IsNull(local_row)) {
                            value = bool_array->Value(local_row) ? 1.0 : 0.0;
                        }
                        break;
                    }
                    default:
                        // For other types (strings, etc.), set to 0.0
                        // This matches Python's behavior when converting non-numeric columns
                        value = 0.0;
                        break;
                }
                
                row_data[col_name] = value;
            }
            
            data.push_back(row_data);
        }
        
    } catch (const std::exception& e) {
        throw std::runtime_error("Error loading parquet file: " + std::string(e.what()));
    }
#else
    // Fallback: try to use a simpler approach or convert to CSV
    throw std::runtime_error(
        "Parquet support not compiled. "
        "Install Apache Arrow: sudo apt-get install libarrow-dev libparquet-dev\n"
        "Or convert parquet to CSV: pandas.read_parquet('file.parquet').to_csv('file.csv')"
    );
#endif
    
    return data;
}

void saveResults(const std::string& filename, const std::vector<float>& scores) {
    std::ofstream file(filename);
    
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file for writing: " + filename);
    }
    
    file << "index,score\n";
    for (size_t i = 0; i < scores.size(); i++) {
        file << i << "," << scores[i] << "\n";
    }
}

std::map<int, std::vector<std::string>> getLayerDict(
    const std::map<std::string, std::vector<std::string>>& features,
    const std::map<std::string, std::vector<std::string>>& connections) {
    
    std::map<int, std::vector<std::string>> layer_dict;
    
    // L0: all feature groups
    for (const auto& [name, _] : features) {
        if (name.find("L0_") == 0) {
            layer_dict[0].push_back(name);
        }
    }
    
    // L1: all L1 connections
    for (const auto& [name, _] : connections) {
        if (name.find("L1_") == 0) {
            layer_dict[1].push_back(name);
        }
    }
    
    // L2: all L2 connections
    for (const auto& [name, _] : connections) {
        if (name.find("L2_") == 0) {
            layer_dict[2].push_back(name);
        }
    }
    
    // L3: all L3 connections (head)
    for (const auto& [name, _] : connections) {
        if (name.find("L3_") == 0) {
            layer_dict[3].push_back(name);
        }
    }
    
    return layer_dict;
}

Metrics calcMetrics(const std::vector<float>& predictions,
                   const std::vector<int>& labels,
                   float threshold,
                   int learn_period) {
    Metrics m;
    if (predictions.size() != labels.size() || predictions.empty()) {
        return m;
    }

    size_t start = 0;
    if (learn_period > 0) {
        start = static_cast<size_t>(learn_period);
        if (start >= predictions.size()) {
            return m;
        }
    }

    int tp = 0, fp = 0, tn = 0, fn = 0;

    for (size_t i = start; i < predictions.size(); i++) {
        bool pred_anomaly = predictions[i] > threshold;
        bool is_anomaly = labels[i] > 0;
        
        if (pred_anomaly && is_anomaly) tp++;
        else if (pred_anomaly && !is_anomaly) fp++;
        else if (!pred_anomaly && !is_anomaly) tn++;
        else fn++;
    }
    
    // Precision: TP / (TP + FP)
    if (tp + fp > 0) {
        m.precision = static_cast<float>(tp) / (tp + fp);
    }
    
    // Recall: TP / (TP + FN)
    if (tp + fn > 0) {
        m.recall = static_cast<float>(tp) / (tp + fn);
    }
    
    // F1: 2 * (precision * recall) / (precision + recall)
    if (m.precision + m.recall > 0) {
        m.f1 = 2.0f * (m.precision * m.recall) / (m.precision + m.recall);
    }
    
    // Accuracy: (TP + TN) / total
    m.accuracy = static_cast<float>(tp + tn) / predictions.size();
    
    return m;
}

GridSearchResult findBestScore(const std::vector<float>& predictions,
                               const std::vector<int>& labels,
                               const std::vector<float>& thresholds,
                               int learn_period) {
    GridSearchResult result;
    result.best.score = 0.0f;
    
    if (predictions.empty() || labels.empty()) {
        return result;
    }
    
    float sum_precision = 0.0f;
    float sum_recall = 0.0f;
    float sum_f1 = 0.0f;
    float sum_accuracy = 0.0f;
    size_t count = 0;
    
    // Grid search over thresholds
    for (float thresh : thresholds) {
        Metrics m = calcMetrics(predictions, labels, thresh, learn_period);
        sum_precision += m.precision;
        sum_recall += m.recall;
        sum_f1 += m.f1;
        sum_accuracy += m.accuracy;
        count++;
        
        // Optimize for F1 score
        if (m.f1 > result.best.score) {
            result.best.score = m.f1;
            result.best.metrics = m;
            result.best.best_threshold = thresh;
        }
    }
    
    result.best.params["threshold"] = result.best.best_threshold;
    result.thresholds_tested = count;
    
    if (count > 0) {
        result.average_metrics.precision = sum_precision / static_cast<float>(count);
        result.average_metrics.recall = sum_recall / static_cast<float>(count);
        result.average_metrics.f1 = sum_f1 / static_cast<float>(count);
        result.average_metrics.accuracy = sum_accuracy / static_cast<float>(count);
    }
    
    return result;
}

// ---------------------------------------------------------------------------
// RowStreamer implementations
// ---------------------------------------------------------------------------

class CSVRowStreamer : public RowStreamer {
public:
    CSVRowStreamer(const std::string& path, int min_row, int max_row, int stride,
                  const std::set<std::string>& required_features)
        : required_features_(required_features), max_row_(max_row), stride_(stride),
          current_file_row_(min_row), last_label_(0), total_rows_(0) {

        file_.open(path);
        if (!file_.is_open()) throw std::runtime_error("Cannot open CSV file: " + path);

        // Parse header
        std::string line;
        if (!std::getline(file_, line)) throw std::runtime_error("Empty CSV file: " + path);
        std::stringstream ss(line);
        std::string col;
        while (std::getline(ss, col, ',')) {
            col.erase(0, col.find_first_not_of(" \t\r\n"));
            col.erase(col.find_last_not_of(" \t\r\n") + 1);
            headers_.push_back(col);
        }

        // Skip to min_row
        for (int i = 0; i < min_row; i++) {
            if (!std::getline(file_, line)) break;
        }

        for (int i = min_row; i < max_row; i += stride) total_rows_++;
    }

    bool hasNext() const override {
        return current_file_row_ < max_row_ && file_.good() && !file_.eof();
    }

    std::map<std::string, double> nextRow() override {
        std::string line;
        std::getline(file_, line);

        std::map<std::string, double> row;
        std::stringstream ss(line);
        std::string value;
        size_t col_idx = 0;
        last_label_ = 0;

        while (std::getline(ss, value, ',') && col_idx < headers_.size()) {
            value.erase(0, value.find_first_not_of(" \t\r\n"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);

            const std::string& col_name = headers_[col_idx];
            double num_val = 0.0;
            try { num_val = std::stod(value); } catch (...) {}

            if (col_name == "label" || col_name == "Label") {
                last_label_ = static_cast<int>(std::lround(num_val));
            }
            if (required_features_.count(col_name)) {
                row[col_name] = num_val;
            }
            col_idx++;
        }

        current_file_row_ += stride_;
        // Skip stride-1 rows to land on the next sampled index
        for (int i = 1; i < stride_; i++) std::getline(file_, line);

        return row;
    }

    int lastLabel() const override { return last_label_; }
    size_t totalRows() const override { return total_rows_; }

private:
    std::ifstream file_;
    std::vector<std::string> headers_;
    std::set<std::string> required_features_;
    int current_file_row_;
    int max_row_;
    int stride_;
    int last_label_;
    size_t total_rows_;
};

#ifdef USE_ARROW
class ParquetRowStreamer : public RowStreamer {
public:
    ParquetRowStreamer(const std::string& path, int min_row, int max_row, int stride,
                      const std::set<std::string>& required_features)
        : required_features_(required_features), stride_(stride),
          current_row_(min_row), last_label_(0), total_rows_(0), label_col_idx_(-1) {

        // Load the Arrow table (columnar — ~10x more memory-efficient than vector<map>).
        // Row extraction happens lazily during run(), one row at a time.
        auto infile = *arrow::io::ReadableFile::Open(path, arrow::default_memory_pool());
        auto reader = *parquet::arrow::OpenFile(infile, arrow::default_memory_pool());
        if (!reader->ReadTable(&table_).ok())
            throw std::runtime_error("Failed to read parquet table: " + path);

        for (int i = 0; i < table_->num_columns(); i++) {
            column_names_.push_back(table_->schema()->field(i)->name());
            if (column_names_.back() == "label" || column_names_.back() == "Label")
                label_col_idx_ = i;
        }

        int actual_max = std::min(max_row, (int)table_->num_rows());
        max_row_ = actual_max;
        for (int i = min_row; i < actual_max; i += stride) total_rows_++;
    }

    bool hasNext() const override { return current_row_ < max_row_; }

    std::map<std::string, double> nextRow() override {
        std::map<std::string, double> row;
        last_label_ = 0;

        for (int col = 0; col < (int)column_names_.size(); col++) {
            const std::string& col_name = column_names_[col];
            bool is_label   = (col == label_col_idx_);
            bool is_required = required_features_.count(col_name) > 0;
            if (!is_required && !is_label) continue;

            double value = extractValue(col, current_row_);
            if (is_label)    last_label_ = static_cast<int>(std::lround(value));
            if (is_required) row[col_name] = value;
        }

        current_row_ += stride_;
        return row;
    }

    int lastLabel() const override { return last_label_; }
    size_t totalRows() const override { return total_rows_; }

private:
    double extractValue(int col_idx, int64_t row_idx) {
        auto column = table_->column(col_idx);
        int64_t offset = 0;
        for (int c = 0; c < column->num_chunks(); c++) {
            auto chunk = column->chunk(c);
            if (row_idx < offset + chunk->length()) {
                int64_t local = row_idx - offset;
                switch (column->type()->id()) {
                    case arrow::Type::DOUBLE:
                        return std::static_pointer_cast<arrow::DoubleArray>(chunk)->Value(local);
                    case arrow::Type::FLOAT:
                        return static_cast<double>(std::static_pointer_cast<arrow::FloatArray>(chunk)->Value(local));
                    case arrow::Type::INT64:
                        return static_cast<double>(std::static_pointer_cast<arrow::Int64Array>(chunk)->Value(local));
                    case arrow::Type::INT32:
                        return static_cast<double>(std::static_pointer_cast<arrow::Int32Array>(chunk)->Value(local));
                    case arrow::Type::BOOL:
                        return std::static_pointer_cast<arrow::BooleanArray>(chunk)->Value(local) ? 1.0 : 0.0;
                    default: return 0.0;
                }
            }
            offset += chunk->length();
        }
        return 0.0;
    }

    std::shared_ptr<arrow::Table> table_;
    std::vector<std::string> column_names_;
    std::set<std::string> required_features_;
    int current_row_;
    int max_row_;
    int stride_;
    int last_label_;
    int label_col_idx_;
    size_t total_rows_;
};
#endif // USE_ARROW

std::unique_ptr<RowStreamer> makeStreamer(
    const std::string& path, int min_row, int max_row, int stride,
    const std::set<std::string>& required_features) {

    if (path.find(".parquet") != std::string::npos) {
#ifdef USE_ARROW
        return std::make_unique<ParquetRowStreamer>(path, min_row, max_row, stride, required_features);
#else
        throw std::runtime_error(
            "Parquet support not compiled. Install Apache Arrow or convert to CSV.");
#endif
    }
    return std::make_unique<CSVRowStreamer>(path, min_row, max_row, stride, required_features);
}

} // namespace htm_swat

