#pragma once

#include <stdint.h>
#include <stddef.h>
#include <cstring>

namespace LedDisplayPolicy {
constexpr uint8_t MAX_ITEMS = 14;
constexpr uint32_t PAGE_INTERVAL_MS = 5000;
constexpr uint32_t FRAME_INTERVAL_MS = 50;
constexpr const char *SENSOR_ORDER[MAX_ITEMS] = {
    "a34001", "a34002", "a34004", "a34005",
    "a01001", "a01002", "a01006", "a01007", "a01008", "L90",
    "a21004", "a21005", "a21026", "w34011"
};

struct Item {
    char text[17] = {};
    bool alarm = false;
};

inline void setItem(Item &item, const char *text, bool alarm) {
    const size_t length = std::strlen(text);
    size_t kept = length > 16 ? 16 : length;
    // Preserve the existing wire encoder's truncation rule.
    if (length > 16 && static_cast<uint8_t>(text[kept - 1]) >= 0x80) --kept;
    std::memcpy(item.text, text, kept);
    item.text[kept] = '\0';
    item.alarm = alarm;
}

constexpr bool due(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
}

class Cache {
public:
    void beginUpdate() { count_ = 0; }
    bool append(const char *text, bool alarm) {
        if (count_ >= MAX_ITEMS) return false;
        setItem(items_[count_++], text, alarm);
        return true;
    }
    uint8_t count() const { return count_; }
    uint8_t pageCount(uint8_t regions) const {
        return static_cast<uint8_t>((count_ + regions - 1) / regions);
    }
    void finishUpdate(uint8_t regions) {
        if (page_ >= pageCount(regions)) page_ = 0;
    }
    void resetPage() { page_ = 0; }
    const Item *slot(uint8_t position, uint8_t regions) const {
        const uint8_t index = page_ * regions + position;
        return index < count_ ? &items_[index] : nullptr;
    }
    void advance(uint8_t regions) {
        const uint8_t pages = pageCount(regions);
        page_ = pages == 0 ? 0 : static_cast<uint8_t>((page_ + 1) % pages);
    }
private:
    Item items_[MAX_ITEMS];
    uint8_t count_ = 0;
    uint8_t page_ = 0;
};

static_assert(sizeof(Item) == 18, "Bounded wire-text cache");
static_assert(sizeof(Cache) <= 256, "LED cache must remain below 256 bytes");
static_assert(due(4, 0xFFFFFFFEU), "Page timing must survive millis wrap");
} // namespace LedDisplayPolicy
