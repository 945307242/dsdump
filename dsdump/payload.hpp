//
//  payload.hpp
//  dsdump
//
//  Created by Derek Selander on 6/10/19.
//  Copyright © 2019 Selander. All rights reserved.
//

#ifndef payload_hpp
#define payload_hpp

#import <type_traits>
#import <stdio.h>
#import <vector>
#import <unordered_map>
#import <map>
#import <string>
#import <mach-o/loader.h>
#import <unordered_set>



#define ARM64E_MASK             0x000007FFFFFFFFFFUL
#define ARM64E_POINTER(data)    ((uintptr_t)data & ARM64E_MASK)

namespace payload {
    
    extern uint8_t *data;
    extern uintptr_t size;
    extern std::vector<struct section_64 *> sections;
    extern uintptr_t offset;
    extern std::map<std::string, struct section_64 *> sectionsDict;

    extern std::unordered_set<char*> filters;
    uintptr_t Offset2Virtual(uintptr_t f);

    template <typename T>
    T* GetData(uintptr_t offset) {
        auto retT = reinterpret_cast<uintptr_t>(&payload::data[offset]);
        return reinterpret_cast<T*>(ARM64E_POINTER(retT)) ;
    }
    
    template <typename T, typename C>
    T Cast(C val) {
        return reinterpret_cast<T>(val);
    }
    
    
    /// Used to translate virtual load addresses to disk (mmap'd) offsets
    template <class T, bool isCodePointer = false>
    struct LoadToDiskTranslator {
        template <typename C>
        static payload::LoadToDiskTranslator<T, isCodePointer>* Cast(C val) {
            return reinterpret_cast<payload::LoadToDiskTranslator<T, isCodePointer>*>(val);
        }
        
        /// This assumes the disk address (the address mmap'd in memory) doesn't overlap with the virtual address
        inline bool isDisk() {
            auto d = reinterpret_cast<uintptr_t>(payload::data);
//            auto v = reinterpret_cast<uintptr_t>(this);
            auto v = strip_PAC();
            return v >= d && v <= (d + payload::size);
        }
        
        inline bool isNull() {
            auto h = reinterpret_cast<uintptr_t>(this);
            // Normal case
            if (h == 0) {
                return true;
            }
            
            // ARM64e case
            if (h & (1UL << 62)) {
                return true;
            }
            return false; 
        }
        
        ///
        uintptr_t strip_PAC() {
            auto p = reinterpret_cast<uintptr_t>(this);
            auto isPACCodePointer =  reinterpret_cast<uintptr_t>(p & (1UL << 63)) ? true : false;
            // Code ARM64e pointer
            if (isPACCodePointer) {

                auto lower32Mask = -1UL >> 32;
                auto virtualAddress = payload::Offset2Virtual(lower32Mask & p);
                return virtualAddress;
            }
            
            // The DATA ARM64e pointer
            return ARM64E_POINTER(p);
        }
        
        ///
        T* strip() {
            auto p = reinterpret_cast<uintptr_t>(this);
            auto isPACCodePointer =  reinterpret_cast<uintptr_t>(p & (3UL << 62)) ? true : false;
            // Code ARM64e pointer
            if (isPACCodePointer ||  isCodePointer) {
                
                auto lower32Mask = -1UL >> 32;
                auto virtualAddress = payload::Offset2Virtual(lower32Mask & p);
                return reinterpret_cast<T*>(virtualAddress);
            }
            
            // The DATA ARM64e pointer
            return reinterpret_cast<T*>(ARM64E_POINTER(p));
        }
        
        ///
        inline bool isLoad() {
            return !isDisk();
        }
        
        ///
        inline T* load() {
            if (isLoad()) {return reinterpret_cast<T*>((uintptr_t)strip_PAC());  }
            auto offset = reinterpret_cast<uintptr_t>(strip_PAC()) - reinterpret_cast<uintptr_t>(payload::data);
            for (auto &sec : payload::sections) {
                if (sec->offset <= (offset) && (offset) < (sec->offset + sec->size)) {
                    auto resolvedLoad = offset - sec->offset + sec->addr;
                    auto payload = reinterpret_cast<T*>(resolvedLoad);
                    return payload;
                }
            }
            ::printf("WARNING: couldn't find address %p in binary!\n", (void*)this);
            return nullptr;
        }
        
        ///
        inline T* disk() {
            // Quiets compiler for null this checks
//            auto thisRef = this;
//            if (thisRef == nullptr) {
//                return nullptr;
//            }

            if (isDisk()) {
                return reinterpret_cast<T*>(ARM64E_POINTER(this));
            }
            
//            auto loadAddress = reinterpret_cast<uintptr_t>(ARM64E_POINTER(this));
            auto loadAddress = strip_PAC();
            for (auto &sec : payload::sections) {
                if (sec->addr <= loadAddress && loadAddress < sec->addr + sec->size) {
                    uintptr_t resolvedOffset = loadAddress - sec->addr  + sec->offset;
                    uint8_t *resolvedAddress = &payload::data[resolvedOffset];
                    auto payload = payload::LoadToDiskTranslator<uintptr_t>::Cast(resolvedAddress)->strip_PAC();
                    return reinterpret_cast<T*>(payload);
                }
            }
            
            ::printf("WARNING: couldn't find address %p (%p) in binary!\n", (void*)this, (void*)loadAddress);
            return nullptr;
        }
        
        
        inline bool validAddress() {
            auto loadAddress = reinterpret_cast<uintptr_t>(ARM64E_POINTER(this));
            for (auto &sec : payload::sections) {
                if (sec->addr <= loadAddress && loadAddress < sec->addr + sec->size) {
                    return true;
                }
            }
            return false;
        }
        
        inline uintptr_t loadAddress() {
            auto diskAddress = reinterpret_cast<payload::LoadToDiskTranslator<uintptr_t, isCodePointer>*>(this->disk());
            return reinterpret_cast<uintptr_t>(diskAddress->load());
        }
        
        // Using blah.atIndex(i) you get ARM64e resolves via the slightly prettier syntax of blah[i]
        inline T Get(int i) {
            return reinterpret_cast<T>(ARM64E_POINTER(this->disk()[i]));
        }
        
        inline T* GetDisk(int i) {
            auto addr = &this->disk()[i];
            return reinterpret_cast<payload::LoadToDiskTranslator<T, isCodePointer>*>(addr)->disk();
        }
    };
    
    inline bool ValidDiskAddress(uintptr_t addr) {
        if ((uintptr_t)payload::data <= addr && addr <= (uintptr_t)payload::data + payload::size) {
            return true;
        }

        return false;
    }
    
    // IF there's a concrete type, then 
    template <class T>
    struct DiskWrapper : payload::LoadToDiskTranslator<T> {
        T val;
        template <class C>
        static payload::DiskWrapper<T>* Cast(C val) {
            return reinterpret_cast<payload::DiskWrapper<T>*>(val);
        }
    };
    
    template <typename T, typename C>
    static payload::LoadToDiskTranslator<T>* CastToDisk(C val) {
        return reinterpret_cast<payload::LoadToDiskTranslator<T>*>(val);
    }

    
    template <typename T>
    uintptr_t GetLoadAddress(T t)  {
        auto diskAddress = reinterpret_cast<payload::LoadToDiskTranslator<uintptr_t>*>(t);
        return reinterpret_cast<uintptr_t>(diskAddress->load());
    }
}
#endif /* payload_hpp */
