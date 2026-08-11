#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include "rt_db_api.h"
}

namespace common {

class RtDbLoggerBase {
public:
    explicit RtDbLoggerBase(const char* sceneName = "Unknown")
        : sceneName_(sceneName) {}

    virtual ~RtDbLoggerBase() { cleanup(); }

    bool initialize() {
        db_ = new rt_db_handle_t();
        std::memset(db_, 0, sizeof(rt_db_handle_t));

        if (!rt_db_init(db_, nullptr)) {
            std::fprintf(stderr, "[%s] RT_DB connection failed — running standalone\n", sceneName_);
            connected_ = false;
            return false;
        }
        connected_ = true;
        buildPointIndexMap();
        std::printf("[%s] RT_DB connected, %zu points mapped\n", sceneName_, pointCount_);
        return true;
    }

    void cleanup() {
        if (db_ && connected_) { rt_db_cleanup(db_); connected_ = false; }
        delete db_; db_ = nullptr;
    }

    bool isConnected() const { return connected_; }

protected:
    virtual void buildPointIndexMap() = 0;

    bool registerPoint(const char* pointId, size_t* outIndex) {
        if (!connected_ || !db_) return false;
        *outIndex = rt_db_find_index_by_id(db_, pointId);
        if (*outIndex == (size_t)-1) {
            std::fprintf(stderr, "[%s] WARNING: point '%s' not found\n", sceneName_, pointId);
            return false;
        }
        pointCount_++;
        return true;
    }

    bool writePoint(size_t index, double value, long quality = 1) {
        if (!connected_ || index == (size_t)-1) return false;
        return rt_db_set_value(db_, index, value, quality);
    }

    bool readPoint(size_t index, double* outValue, long* outQuality = nullptr) {
        if (!connected_ || index == (size_t)-1) return false;
        return rt_db_get_value(db_, index, outValue, outQuality, nullptr);
    }

    bool popCommand(ControlCommand* cmd) {
        if (!connected_ || !db_) return false;
        return rt_db_pop_command(db_, cmd);
    }

    rt_db_handle_t* db_ = nullptr;
    bool connected_ = false;
    const char* sceneName_;
    size_t pointCount_ = 0;
};

} // namespace common
