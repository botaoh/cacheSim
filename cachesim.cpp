#include "cachesim.hpp"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cinttypes>

/*-------------DO NOT CHANGE THIS BLOCK OF CODE-------------*/
//Pseudo-Random Number Generator for RANDOM Replacement policy
static unsigned long int evict_random_next = 1;
int evict_random(void)
{
    evict_random_next = evict_random_next * 1103515243 + 12345;
    return (unsigned int)(evict_random_next / 65536) % 32768;
}

void evict_srand(unsigned int seed)
{
    evict_random_next = seed;
}
/*----------------------------------------------------------*/

struct CacheBlock{
    uint64_t tag;
    bool valid;
    bool dirty;
    bool operator==(const CacheBlock& other) const {
        return tag == other.tag && valid == other.valid && dirty == other.dirty;
    }
};

struct VictimBlock {
    uint64_t tag;
    uint64_t index;
    bool valid;
    bool dirty;
    bool operator==(const VictimBlock& other) const {
        return tag == other.tag && index == other.index && valid == other.valid && dirty == other.dirty;
    }    
};

typedef std::vector<CacheBlock> CacheSet;
std::vector<CacheSet> L1_cache;
std::vector<VictimBlock> victim_cache;
std::vector<CacheSet> L2_cache;
sim_config_t m_config;

uint64_t early_restart_offset_sum = 0;
uint64_t early_restart_offset_count = 0;

/**
 * Subroutine for initializing the cache simulator. You many add and initialize any global or heap
 * variables as needed.
 * TODO: You're responsible for completing this routine
 */

void sim_setup(sim_config_t *config) {
    m_config = *config;
    // L1 Cache: (l1_config->c, l1_config->b, l1_config->s)
    uint64_t l1CacheTagBit = m_config.l1_config.c - m_config.l1_config.b - m_config.l1_config.s;
    uint64_t l1CacheNumSets = 1 << l1CacheTagBit;
    L1_cache.resize(l1CacheNumSets);
    for (uint64_t i = 0; i < l1CacheNumSets; i++) {
        L1_cache[i].resize(1 << m_config.l1_config.s, {0, false, false});
    }
    // Victim Cache: config->victim_cache_entries
    if (config->victim_cache_entries > 0) {
        victim_cache.resize(config->victim_cache_entries, {0, 0, false, false});
    }
    if (!m_config.l2_config.disabled) {
        uint64_t l2CacheTagBit = m_config.l2_config.c - m_config.l2_config.b - m_config.l2_config.s;
        uint64_t l2CacheNumSets = 1 << l2CacheTagBit;
        L2_cache.resize(l2CacheNumSets);
        for (uint64_t i = 0; i < l2CacheNumSets; i++) {
            L2_cache[i].resize(1 << m_config.l2_config.s, {0, false, false});
        }    
    }
}

/**
 * Subroutine that simulates the cache one trace event at a time.
 * TODO: You're responsible for completing this routine
 */
void sim_access(char rw, uint64_t addr, sim_stats_t* stats) {
    if (rw == 'R') stats->reads++;
    else if (rw == 'W') stats->writes++;

    stats->accesses_l1++;

    // Judge: Found in L1 Cache?
    uint64_t l1_victim_block_offset_bits = m_config.l1_config.b;
    uint64_t l1_victim_index_bits = m_config.l1_config.c - m_config.l1_config.b - m_config.l1_config.s;

    uint64_t l1_victim_tag = addr >> (l1_victim_block_offset_bits + l1_victim_index_bits);
    uint64_t l1_victim_index = (addr >> l1_victim_block_offset_bits) & ((1 << l1_victim_index_bits) - 1);
    if (l1_victim_index_bits == 0) l1_victim_index = 0;

    uint64_t l2_block_offset_bits = m_config.l2_config.b;
    uint64_t l2_index_bits = m_config.l2_config.c - m_config.l2_config.b - m_config.l2_config.s;

    uint64_t l2_tag = addr >> (l2_block_offset_bits + l2_index_bits);
    uint64_t l2_index = (addr >> l2_block_offset_bits) & ((1 << l2_index_bits) - 1);

    CacheSet &l1_set = L1_cache[l1_victim_index];
    CacheSet &l2_set = L2_cache[l2_index];

    // if (stats->accesses_l1 - 1>= 6766 && stats->accesses_l1 - 1 <= 40000) {
    //     printf("L1 decomposed address 0x%lx -> Tag: 0x%lx and Index: 0x%lx\n", addr, l1_victim_tag, l1_victim_index);
    //     printf("L2 decomposed address 0x%lx -> Tag: 0x%lx and Index: 0x%lx\n", addr, l2_tag, l2_index);
    //     printf("%d: Tag:  ", stats->accesses_l1 - 1);
    //     if (!m_config.l2_config.disabled) {
    //         CacheSet t = L2_cache[49];
    //         for (int i = 0; i < t.size(); i++) {
    //             printf("0x%lx ", t[i].tag);
    //         }
    //         printf("\n");
    //     }
    // }

    CacheBlock* l1_found_block = nullptr;
    for (CacheBlock &l1_block : l1_set) {
        if (l1_block.valid && l1_block.tag == l1_victim_tag) {
            l1_found_block = &l1_block;
            break;
        }
    }

    if (l1_found_block) {
        // L1 Cache Hit
        #ifdef DEBUG
        printf("%d: L1 hit\n", stats->accesses_l1-1);
        #endif    
        stats->hits_l1++;
        if (rw == 'W') l1_found_block->dirty = true;

        CacheBlock newFoundBlock = *l1_found_block;

        // Move block to MRU position
        l1_set.erase(std::remove(l1_set.begin(), l1_set.end(), *l1_found_block), l1_set.end());
        l1_set.insert(l1_set.begin(), newFoundBlock);
        return;
    } 
    else {
        stats->misses_l1++;

        // Find in Victim Cache
        VictimBlock* found_victim_block = NULL;
        bool victimDirty = false;
        bool victimIsFound = false;
        if (m_config.victim_cache_entries > 0) {
            for (VictimBlock &block : victim_cache) {
                if (block.valid && block.tag == l1_victim_tag && block.index == l1_victim_index) {
                    found_victim_block = &block;
                    victimDirty |= block.dirty;
                    break;
                }
            }

            CacheBlock newBlock;
            newBlock.dirty = ((rw == 'W') || victimDirty);
            newBlock.valid = true;
            newBlock.tag = l1_victim_tag;

            if (found_victim_block) {
                victimIsFound = true;
                stats->hits_victim_cache++;
                // Transferred to L1 Cache!
                // Judge: Any element in L1 set invalid?
                int invalidIndexInL1 = -1;
                for (int i = 0; i < l1_set.size(); i++) {
                    if (l1_set[i].valid == false) {
                        invalidIndexInL1 = i;
                        break;
                    }
                }
                if (invalidIndexInL1 != -1) {
                    // Move to MRU
                    l1_set.erase(l1_set.begin() + invalidIndexInL1);
                    l1_set.insert(l1_set.begin(), newBlock);
                    victim_cache.erase(std::remove(victim_cache.begin(), victim_cache.end(), *found_victim_block), victim_cache.end());
                }
                else {
                    // A literal swap: l1[last] and found victim cache
                    int invalidIndexInL1 = l1_set.size()-1;
                    VictimBlock nvBlock;
                    nvBlock.dirty = l1_set[invalidIndexInL1].dirty;
                    nvBlock.valid = l1_set[invalidIndexInL1].valid;
                    nvBlock.tag = l1_set[invalidIndexInL1].tag;
                    nvBlock.index = l1_victim_index;   
                    victim_cache.erase(std::remove(victim_cache.begin(), victim_cache.end(), *found_victim_block), victim_cache.end());
                    victim_cache.insert(victim_cache.begin(), nvBlock); 
                    l1_set.erase(l1_set.begin() + invalidIndexInL1);
                    l1_set.insert(l1_set.begin(), newBlock);
                }
                return;
            }

            else {
                victimIsFound = false;
                stats->misses_victim_cache++;
            }
        }
        else {
            stats->misses_victim_cache++;
        }

        stats->reads_l2++;
        if (m_config.l2_config.disabled) {
            stats->read_misses_l2++;
        }

        // Try finding in L2 Cache!
        if (!victimIsFound && !m_config.l2_config.disabled) {
            // Block in L2 Cache?
            int foundId = -1;
            for (int i = 0; i < l2_set.size(); i++) {
                if (l2_set[i].valid && l2_set[i].tag == l2_tag) {
                    foundId = i;
                    break;
                }
            }

            if (foundId != -1) {
                #ifdef DEBUG
                printf("%d: L2 read hit\n", stats->accesses_l1-1);
                #endif            
                stats->read_hits_l2++;
                switch (m_config.l2_config.replace_policy)
                {
                    case REPLACEMENT_POLICY_MIP:
                    case REPLACEMENT_POLICY_LIP:
                        std::rotate(l2_set.begin(), l2_set.begin() + foundId, l2_set.begin() + foundId + 1);
                        #ifdef DEBUG
                        printf("%d: 2 In L2, moving Tag: 0x%" PRIx64 " and Index: 0x%" PRIx64 " to MRU position\n", stats->accesses_l1-1, l2_set[0].tag, l2_index);
                        #endif
                        break;
                    case REPLACEMENT_POLICY_RANDOM:
                    case REPLACEMENT_POLICY_FIFO:
                        break;
                    default:
                        break;          
                }
            }

            else {
                #ifdef DEBUG
                printf("%d: L2 read miss\n", stats->accesses_l1-1);
                #endif            
                stats->read_misses_l2++;

                if (m_config.l2_config.enable_ER) {
                    uint64_t word_offset = (addr & ((1 << m_config.l2_config.b) - 1)) / WORD_SIZE;
                    early_restart_offset_sum += word_offset;
                    early_restart_offset_count++;
                }
        
                if (true) {
                    int invalidIndexInL2 = -1;
                    for (int i = 0; i < l2_set.size(); i++) {
                        if (l2_set[i].valid == false) {
                            invalidIndexInL2 = i;
                            break;
                        }
                    }
                    if (invalidIndexInL2 == -1) {
                        // Delete L2 Victim block:
                        switch (m_config.l2_config.replace_policy)
                        {
                            case REPLACEMENT_POLICY_MIP:
                            case REPLACEMENT_POLICY_LIP:
                            case REPLACEMENT_POLICY_FIFO:
                                invalidIndexInL2 = l2_set.size()-1;
                                break;
                            case REPLACEMENT_POLICY_RANDOM:
                                if (m_config.l2_config.s == 0) invalidIndexInL2 = 0;
                                else invalidIndexInL2 = (evict_random() % ((1 << m_config.l2_config.s)- 1));
                                break;
                            default:
                                break;          
                        }
                    }
                    #ifdef DEBUG
                    // printf("L1 decomposed address 0x%lx -> Tag: 0x%lx and Index: 0x%lx\n", addr, l1_victim_tag, l1_victim_index);
                    printf("Evict from L2: block with valid=%d and index=0x%lx\n", l2_set[invalidIndexInL2].valid, l2_index);
                    #endif
                    l2_set.erase(l2_set.begin() + invalidIndexInL2);
        
                    // Create a new cache block to insert
                    CacheBlock l2NewCacheBlock;
                    l2NewCacheBlock.dirty = false;
                    l2NewCacheBlock.valid = true;
                    l2NewCacheBlock.tag = l2_tag;
        
                    switch (m_config.l2_config.replace_policy)
                    {
                        case REPLACEMENT_POLICY_MIP:
                            l2_set.insert(l2_set.begin(), l2NewCacheBlock);
                            break;
                        case REPLACEMENT_POLICY_LIP:
                            l2_set.insert(l2_set.end(), l2NewCacheBlock);
                            break;
                        case REPLACEMENT_POLICY_FIFO:
                            l2_set.insert(l2_set.begin(), l2NewCacheBlock);
                            break;
                        case REPLACEMENT_POLICY_RANDOM:
                            l2_set.insert(l2_set.end(), l2NewCacheBlock);
                            break;
                        default:
                            break;          
                    } 
                }
            }
        }
        // Insert the block to L1 Cache
        CacheBlock newBlock;
        newBlock.dirty = ((rw == 'W') || victimDirty);
        newBlock.valid = true;
        newBlock.tag = l1_victim_tag;        

        // Judge: Any element in L1 set invalid?
        int invalidIndexInL1 = -1;
        for (int i = 0; i < l1_set.size(); i++) {
            if (l1_set[i].valid == false) {
                invalidIndexInL1 = i;
                break;
            }
        }

        if (invalidIndexInL1 != -1) {
            // Move to MRU
            l1_set.erase(l1_set.begin() + invalidIndexInL1);
            l1_set.insert(l1_set.begin(), newBlock);
            return;
        }
        else {
            if (m_config.victim_cache_entries > 0) {
                int victimIndexInL1 = l1_set.size()-1;
                VictimBlock nvBlock;
                nvBlock.dirty = l1_set[victimIndexInL1].dirty;
                nvBlock.valid = l1_set[victimIndexInL1].valid;
                nvBlock.tag = l1_set[victimIndexInL1].tag;
                nvBlock.index = l1_victim_index;   

                // Insert nvBlock to victim cache
                int victimInvalidIndex = -1;
                for (int i = 0; i < victim_cache.size(); i++) {
                    if (!victim_cache[i].valid) {
                        victimInvalidIndex = i;
                        break;
                    }
                }
                if (victimInvalidIndex != -1) {
                    victim_cache.erase(victim_cache.begin() + victimInvalidIndex);
                    victim_cache.insert(victim_cache.begin(), nvBlock);
                }
                else {
                    victimInvalidIndex = victim_cache.size()-1;
                    if (victim_cache[victimInvalidIndex].dirty) {
                        stats->write_backs_l1_or_victim_cache++;
                        stats->writes_l2++;
                    }
                    // Try inserting this block to L2 Cache!
                    if (!m_config.l2_config.disabled) {
                        uint64_t victim_block_tag = victim_cache[victimInvalidIndex].tag;
                        uint64_t victim_block_index = victim_cache[victimInvalidIndex].index;
                        uint64_t victim_block_addr_except_offset = (victim_block_tag << (m_config.l1_config.c - m_config.l1_config.b - m_config.l1_config.s)) | victim_block_index;
                        uint64_t victim_block_l2_tag = victim_block_addr_except_offset >> (m_config.l2_config.c - m_config.l2_config.b - m_config.l2_config.s);
                        uint64_t victim_block_l2_index = victim_block_addr_except_offset & ((1 << (m_config.l2_config.c - m_config.l2_config.b - m_config.l2_config.s))-1);
                        if (victim_cache[victimInvalidIndex].dirty) {
                                
                            CacheSet& l2_victim_set = L2_cache[victim_block_l2_index];
                            int foundId = -1;
                            for (int i = 0; i < l2_victim_set.size(); i++) {
                                if (l2_victim_set[i].tag == victim_block_l2_tag) {
                                    foundId = i;
                                    break;
                                }
                            }
                            if (foundId != -1) {
                                CacheBlock newBlock_l2 = l2_victim_set[foundId];
                                switch (m_config.l2_config.replace_policy)
                                {
                                    case REPLACEMENT_POLICY_MIP:
                                        l2_victim_set.erase(l2_victim_set.begin() + foundId);
                                        l2_victim_set.insert(l2_victim_set.begin(), newBlock_l2);   
                                        break;
                                    case REPLACEMENT_POLICY_LIP:
                                        l2_victim_set.erase(l2_victim_set.begin() + foundId);
                                        l2_victim_set.insert(l2_victim_set.begin(), newBlock_l2);   
                                        break;
                                    case REPLACEMENT_POLICY_FIFO:
                                        break;
                                    case REPLACEMENT_POLICY_RANDOM:
                                        break;
                                    default:
                                        break;          
                                }
                                #ifdef DEBUG
                                // printf("%d: In L2, moving Tag: 0x%" PRIx64 " and Index: 0x%" PRIx64 " to MRU position\n", stats->accesses_l1-1, l2_victim_set[0].tag, l2_index);
                                #endif
                            }
                            else {
                                // Write to DRAM        
                            }
                        }
                    }
                    victim_cache.erase(victim_cache.begin() + victimInvalidIndex);
                    victim_cache.insert(victim_cache.begin(), nvBlock);
                }

                l1_set.erase(l1_set.begin() + victimIndexInL1);
                l1_set.insert(l1_set.begin(), newBlock);
                return;
            }
            else {
                // Switch L1 victim to L2!
                // MRU: switch last element in L1 cache block
                int victimIndex = l1_set.size()-1;
                #ifdef DEBUG
                printf("%d: Evict from L1: block with valid=%d, dirty=%d, tag 0x%lx, and index=0x%lx\n", stats->accesses_l1-1, l1_set[victimIndex].valid, l1_set[victimIndex].dirty, l1_set[victimIndex].tag, victimIndex);
                #endif
                if (l1_set[victimIndex].dirty) {
                    stats->write_backs_l1_or_victim_cache++;
                    stats->writes_l2++;
                    if (!m_config.l2_config.disabled) {
                        int l1VictimIndex = l1_set.size()-1;
                        uint64_t l1_tag = l1_set[l1VictimIndex].tag;
                        uint64_t l1_addr_except_offset = (l1_tag << (m_config.l1_config.c - m_config.l1_config.b - m_config.l1_config.s)) | l1_victim_index;
                        uint64_t l2_tag = l1_addr_except_offset >> (m_config.l2_config.c - m_config.l2_config.b - m_config.l2_config.s);
                        uint64_t l2_index = l1_addr_except_offset & ((1 << (m_config.l2_config.c - m_config.l2_config.b - m_config.l2_config.s))-1);
                        
                        CacheSet& l2_victim_set = L2_cache[l2_index];
                        int foundId = -1;
                        for (int i = 0; i < l2_victim_set.size(); i++) {
                            if (l2_victim_set[i].tag == l2_tag) {
                                foundId = i;
                                break;
                            }
                        }
                        if (foundId != -1) {
                            CacheBlock newBlock_l2 = l2_victim_set[foundId];
                            switch (m_config.l2_config.replace_policy)
                            {
                                case REPLACEMENT_POLICY_MIP:
                                    l2_victim_set.erase(l2_victim_set.begin() + foundId);
                                    l2_victim_set.insert(l2_victim_set.begin(), newBlock_l2);   
                                    break;
                                case REPLACEMENT_POLICY_LIP:
                                    l2_victim_set.erase(l2_victim_set.begin() + foundId);
                                    l2_victim_set.insert(l2_victim_set.begin(), newBlock_l2);   
                                    break;
                                case REPLACEMENT_POLICY_FIFO:
                                    break;
                                case REPLACEMENT_POLICY_RANDOM:
                                    break;
                                default:
                                    break;          
                            }

                            #ifdef DEBUG
                            // printf("%d: In L2, moving Tag: 0x%" PRIx64 " and Index: 0x%" PRIx64 " to MRU position\n", stats->accesses_l1-1, l2_victim_set[0].tag, l2_index);
                            #endif
                        }
                        else {
                            // Write to DRAM        
                        }
                    }
                }
                l1_set.erase(l1_set.begin() + victimIndex);
                l1_set.insert(l1_set.begin(), newBlock);
            }
        }
    }
}

/**
 * Subroutine for cleaning up any outstanding memory operations and calculating overall statistics
 * such as miss rate or average access time.
 * TODO: You're responsible for completing this routine
 */
void sim_finish(sim_stats_t *stats) {
    stats->hit_ratio_l1 = 1.0 * stats->hits_l1 / stats->accesses_l1;
    stats->miss_ratio_l1 = 1 - stats->hit_ratio_l1;
    stats->hit_ratio_victim_cache = 1.0 * stats->hits_victim_cache / (stats->hits_victim_cache + stats->misses_victim_cache);
    stats->miss_ratio_victim_cache = 1.0 * stats->misses_victim_cache / (stats->hits_victim_cache + stats->misses_victim_cache);
    stats->read_hit_ratio_l2 = 1.0 * stats->read_hits_l2 / stats->reads_l2;
    stats->read_miss_ratio_l2 = 1 - stats->read_hit_ratio_l2;
    double hit_time_l1 = L1_HIT_TIME_CONST + (m_config.l1_config.s * L1_HIT_TIME_PER_S);
    double hit_time_l2 = L2_HIT_TIME_CONST + (m_config.l2_config.s * L2_HIT_TIME_PER_S);

    double average_early_restart_offset;
    if (m_config.l2_config.enable_ER && early_restart_offset_count > 0) {
        average_early_restart_offset = (double) early_restart_offset_sum / early_restart_offset_count;
    } else {
        average_early_restart_offset = (1 << m_config.l2_config.b) / WORD_SIZE;
    }

    if (!m_config.l2_config.disabled) {
        double dram_time = DRAM_AT + (DRAM_AT_PER_WORD * (1 << m_config.l2_config.b) / WORD_SIZE);
        double dram_time_er = DRAM_AT + (DRAM_AT_PER_WORD * average_early_restart_offset);
        if (m_config.l2_config.enable_ER) {
            stats->avg_access_time_l2 = hit_time_l2 + stats->read_miss_ratio_l2 * dram_time_er;
        } else {
            stats->avg_access_time_l2 = hit_time_l2 + stats->read_miss_ratio_l2 * dram_time;
        }
        stats->avg_access_time_l1 = hit_time_l1 + (stats->miss_ratio_victim_cache * stats->miss_ratio_l1 * stats->avg_access_time_l2);
    }
    else {
        stats->avg_access_time_l2 = DRAM_AT + DRAM_AT_PER_WORD * (1 << m_config.l2_config.b) / WORD_SIZE;
        stats->avg_access_time_l1 = hit_time_l1 + stats->miss_ratio_l1 * stats->miss_ratio_victim_cache * stats->avg_access_time_l2;
    }
}
