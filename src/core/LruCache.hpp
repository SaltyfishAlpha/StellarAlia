#pragma once

#include <functional>
#include <list>
#include <unordered_map>
#include <utility>

// Header-only LRU cache with O(1) get, put, erase, and clear.
// An EvictCallback is invoked on every eviction (capacity overflow, Erase, or Clear).
template<typename K, typename V>
class LruCache {
public:
    using EvictCallback = std::function<void(const K&, V&)>;

    explicit LruCache(size_t capacity = 0, EvictCallback onEvict = {})
        : m_capacity(capacity), m_onEvict(std::move(onEvict)) {}

    // Returns a pointer to the value if found (and touches it to MRU position),
    // or nullptr on a cache miss.
    V* Get(const K& key) {
        auto it = m_map.find(key);
        if (it == m_map.end()) return nullptr;
        m_list.splice(m_list.begin(), m_list, it->second);
        return &it->second->second;
    }

    // Inserts or updates key→value.  If capacity is exceeded the LRU entry is evicted first.
    void Put(const K& key, V value) {
        auto it = m_map.find(key);
        if (it != m_map.end()) {
            it->second->second = std::move(value);
            m_list.splice(m_list.begin(), m_list, it->second);
            return;
        }
        if (m_capacity > 0 && m_list.size() >= m_capacity)
            evictLRU();
        m_list.emplace_front(key, std::move(value));
        m_map[key] = m_list.begin();
    }

    // Removes a single entry and fires onEvict.
    void Erase(const K& key) {
        auto it = m_map.find(key);
        if (it == m_map.end()) return;
        if (m_onEvict) m_onEvict(it->second->first, it->second->second);
        m_list.erase(it->second);
        m_map.erase(it);
    }

    // Removes all entries and fires onEvict for each.
    void Clear() {
        if (m_onEvict) {
            for (auto& [k, v] : m_list)
                m_onEvict(k, v);
        }
        m_list.clear();
        m_map.clear();
    }

    [[nodiscard]] size_t Size()             const { return m_list.size(); }
    [[nodiscard]] bool   Contains(const K& key) const { return m_map.count(key) != 0; }

private:
    using List = std::list<std::pair<K, V>>;
    List                                           m_list;
    std::unordered_map<K, typename List::iterator> m_map;
    size_t        m_capacity;
    EvictCallback m_onEvict;

    void evictLRU() {
        auto last = std::prev(m_list.end());
        if (m_onEvict) m_onEvict(last->first, last->second);
        m_map.erase(last->first);
        m_list.erase(last);
    }
};
