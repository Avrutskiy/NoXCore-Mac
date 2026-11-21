#import <Foundation/Foundation.h>
#import <Security/Security.h>
#include "noxcore/io/wol_login.hpp"

#include <algorithm>
#include <charconv>
#include <string_view>

namespace nox::io {
namespace {
NSMutableDictionary* loginQuery()
{
    return [@{(__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
              (__bridge id)kSecAttrService: @"io.github.mordentral.noxcore.wol"} mutableCopy];
}

// List metadata once. Request secret data only when the login screen needs it.
std::vector<SavedLogin> readSlots(bool passwords)
{
    NSMutableDictionary* query = loginQuery();
    query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitAll;
    query[(__bridge id)kSecReturnAttributes] = @YES;
    CFTypeRef result = nullptr;
    std::vector<SavedLogin> slots(kUnsavedLoginSlot + 1);
    if (SecItemCopyMatching((__bridge CFDictionaryRef)query, &result) != errSecSuccess) return slots;
    NSArray* items = CFBridgingRelease(result);
    for (NSDictionary* item in items) {
        NSString* account = item[(__bridge id)kSecAttrAccount];
        NSString* nick = item[(__bridge id)kSecAttrLabel];
        if (!account.UTF8String || !nick.UTF8String) continue;
        std::string_view key(account.UTF8String);
        int index = -1;
        const auto [end, error] = std::from_chars(key.data(), key.data() + key.size(), index);
        if (error != std::errc{} || end != key.data() + key.size() || index < 0 || index > kUnsavedLoginSlot) continue;
        slots[index].nick = nick.UTF8String;
        if (passwords) {
            NSMutableDictionary* one = loginQuery();
            one[(__bridge id)kSecAttrAccount] = account;
            one[(__bridge id)kSecAttrLabel] = nick; // Skip a slot replaced since the listing.
            one[(__bridge id)kSecReturnData] = @YES;
            CFTypeRef value = nullptr;
            if (SecItemCopyMatching((__bridge CFDictionaryRef)one, &value) == errSecSuccess) {
                NSData* data = CFBridgingRelease(value);
                if (data.length) slots[index].password.assign(static_cast<const char*>(data.bytes), data.length);
            }
        }
    }
    return slots;
}
}

std::vector<SavedLogin> savedLogins()
{
    @autoreleasepool { return readSlots(true); }
}

bool saveLogin(int index, const SavedLogin& login)
{
    if (index < 0 || index > kUnsavedLoginSlot) return false;
    @autoreleasepool {
        NSString* nick = [NSString stringWithUTF8String:login.nick.c_str()];
        if (!nick) return false;
        NSMutableDictionary* query = loginQuery();
        query[(__bridge id)kSecAttrAccount] = @(index).stringValue;
        NSDictionary* content = @{(__bridge id)kSecAttrLabel: nick,
            (__bridge id)kSecValueData: [NSData dataWithBytes:login.password.data() length:login.password.size()]};
        NSMutableDictionary* item = [query mutableCopy];
        [item addEntriesFromDictionary:content];
        OSStatus status = SecItemAdd((__bridge CFDictionaryRef)item, nullptr);
        if (status == errSecDuplicateItem) {
            status = SecItemUpdate((__bridge CFDictionaryRef)query, (__bridge CFDictionaryRef)content);
        }
        return status == errSecSuccess;
    }
}

std::vector<std::string> savedOnlineNicks()
{
    @autoreleasepool {
        const auto slots = readSlots(false);
        std::vector<std::string> names;
        for (int i = 0; i < kSavedLoginSlots; ++i) {
            const auto& nick = slots[i].nick;
            if (!nick.empty() && std::find(names.begin(), names.end(), nick) == names.end()) names.push_back(nick);
        }
        return names;
    }
}
} // namespace nox::io
