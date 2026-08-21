// Copyright 2022 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include <jni.h>

#include "core/cheats/cheat_base.h"
#include "core/cheats/cheats.h"
#include "core/cheats/memory_search.h"
#include "core/core.h"
#include "core/loader/loader.h"
#include "core/memory.h"
#include "jni/cheats/cheat.h"
#include "jni/id_cache.h"
#include "jni/native_state.h"
#include "network/network.h"

namespace {

constexpr jlong SEARCH_NO_SESSION = -1;
constexpr jlong SEARCH_NO_GAME = -2;
constexpr jlong SEARCH_ONLINE_BLOCKED = -3;
constexpr jlong SEARCH_NOT_PAUSED = -4;
constexpr jlong SEARCH_TITLE_CHANGED = -5;
constexpr jlong SEARCH_TOO_MANY = -6;
constexpr jlong SEARCH_INVALID_VALUE = -7;
constexpr std::size_t MAX_SEARCH_CANDIDATES = 1'000'000;

struct UndoWrite {
    u64 title_id{};
    u64 session_id{};
    VAddr address{};
    Cheats::MemorySearchValueSize value_size{};
    u64 original_value{};
    u64 written_value{};
};

Cheats::MemorySearch memory_search;
std::optional<u64> memory_search_title_id;
std::optional<u64> memory_search_session_id;
std::optional<UndoWrite> undo_write;
std::mutex memory_search_mutex;

bool SearchMatchesSession(const std::optional<u64>& title_id, u64 session_id) {
    return title_id.has_value() && title_id == memory_search_title_id &&
           session_id == memory_search_session_id;
}

bool ClearStaleSessionState(const std::optional<u64>& title_id, u64 session_id) {
    const bool invalidated_search =
        memory_search.IsActive() && !SearchMatchesSession(title_id, session_id);
    if (invalidated_search) {
        memory_search.Reset();
        memory_search_title_id.reset();
        memory_search_session_id.reset();
    }
    if (undo_write.has_value() &&
        (!title_id.has_value() || undo_write->title_id != *title_id ||
         undo_write->session_id != session_id)) {
        undo_write.reset();
    }
    return invalidated_search;
}

std::optional<u64> GetRunningTitleId() {
    Core::System& system = Core::System::GetInstance();
    if (!system.IsPoweredOn()) {
        return std::nullopt;
    }
    u64 title_id{};
    if (system.GetAppLoader().ReadProgramId(title_id) != Loader::ResultStatus::Success) {
        return std::nullopt;
    }
    return title_id;
}

jlong CheckSearchAvailability() {
    if (!GetRunningTitleId().has_value()) {
        return SEARCH_NO_GAME;
    }
    if (auto member = Network::GetRoomMember().lock();
        member && (member->GetState() == Network::RoomMember::State::Joining ||
                   member->IsConnected())) {
        return SEARCH_ONLINE_BLOCKED;
    }
    if (!AndroidNativeState::IsEmulationPaused()) {
        return SEARCH_NOT_PAUSED;
    }
    return 0;
}

std::optional<Cheats::MemorySearchValueSize> ToValueSize(jint value_size) {
    switch (value_size) {
    case 1:
        return Cheats::MemorySearchValueSize::Uint8;
    case 2:
        return Cheats::MemorySearchValueSize::Uint16;
    case 4:
        return Cheats::MemorySearchValueSize::Uint32;
    default:
        return std::nullopt;
    }
}

std::optional<Cheats::MemorySearchComparison> ToComparison(jint comparison) {
    switch (comparison) {
    case 0:
        return Cheats::MemorySearchComparison::Exact;
    case 1:
        return Cheats::MemorySearchComparison::Changed;
    case 2:
        return Cheats::MemorySearchComparison::Unchanged;
    case 3:
        return Cheats::MemorySearchComparison::Increased;
    case 4:
        return Cheats::MemorySearchComparison::Decreased;
    default:
        return std::nullopt;
    }
}

bool IsSearchablePage(Memory::PageType type) {
    return type == Memory::PageType::Memory || type == Memory::PageType::MemoryWatchpoint;
}

std::optional<u64> ReadSearchValue(Memory::MemorySystem& memory,
                                   const std::shared_ptr<Memory::PageTable>& page_table,
                                   VAddr address, Cheats::MemorySearchValueSize value_size) {
    const std::size_t value_bytes = Cheats::MemorySearch::ValueBytes(value_size);
    if (value_bytes == 0 || (address & (value_bytes - 1)) != 0 ||
        (address & Memory::CITRA_PAGE_MASK) + value_bytes > Memory::CITRA_PAGE_SIZE) {
        return std::nullopt;
    }
    const std::size_t page_index = address >> Memory::CITRA_PAGE_BITS;
    if (!IsSearchablePage(page_table->attributes[page_index])) {
        return std::nullopt;
    }
    const u8* pointer = memory.GetPointer(address);
    if (!pointer) {
        return std::nullopt;
    }
    u64 value{};
    for (std::size_t byte = 0; byte < value_bytes; ++byte) {
        value |= static_cast<u64>(pointer[byte]) << (byte * 8);
    }
    return value;
}

bool WriteSearchValue(Core::System& system, VAddr address,
                      Cheats::MemorySearchValueSize value_size, u64 value) {
    switch (value_size) {
    case Cheats::MemorySearchValueSize::Uint8:
        system.Memory().Write8(address, static_cast<u8>(value));
        break;
    case Cheats::MemorySearchValueSize::Uint16:
        system.Memory().Write16(address, static_cast<u16>(value));
        break;
    case Cheats::MemorySearchValueSize::Uint32:
        system.Memory().Write32(address, static_cast<u32>(value));
        break;
    }
    system.InvalidateCacheRange(address, Cheats::MemorySearch::ValueBytes(value_size));
    const auto page_table = system.Memory().GetCurrentPageTable();
    return page_table && ReadSearchValue(system.Memory(), page_table, address, value_size) == value;
}

} // namespace

extern "C" {

static Cheats::CheatEngine& GetEngine() {
    Core::System& system{Core::System::GetInstance()};
    return system.CheatEngine();
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_loadCheatFile(
    JNIEnv* env, jclass, jlong title_id) {
    GetEngine().LoadCheatFile(title_id);
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_saveCheatFile(
    JNIEnv* env, jclass, jlong title_id) {
    GetEngine().SaveCheatFile(title_id);
}

JNIEXPORT jobjectArray JNICALL
Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_getCheats(JNIEnv* env, jclass) {
    auto cheats = GetEngine().GetCheats();

    const jobjectArray array =
        env->NewObjectArray(static_cast<jsize>(cheats.size()), IDCache::GetCheatClass(), nullptr);

    jsize i = 0;
    for (auto& cheat : cheats)
        env->SetObjectArrayElement(array, i++, CheatToJava(env, std::move(cheat)));

    return array;
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_addCheat(
    JNIEnv* env, jclass, jobject j_cheat) {
    auto cheat = *CheatFromJava(env, j_cheat);
    GetEngine().AddCheat(std::move(cheat));
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_removeCheat(
    JNIEnv* env, jclass, jint index) {
    GetEngine().RemoveCheat(index);
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_updateCheat(
    JNIEnv* env, jclass, jint index, jobject j_new_cheat) {
    auto cheat = *CheatFromJava(env, j_new_cheat);
    GetEngine().UpdateCheat(index, std::move(cheat));
}

JNIEXPORT jlong JNICALL
Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_getMemorySearchStatus(JNIEnv*, jclass) {
    const jlong availability = CheckSearchAvailability();
    if (availability != 0) {
        return availability;
    }

    std::scoped_lock lock{memory_search_mutex};
    const std::optional<u64> title_id = GetRunningTitleId();
    const u64 session_id = AndroidNativeState::GetEmulationSessionId();
    if (ClearStaleSessionState(title_id, session_id)) {
        return SEARCH_TITLE_CHANGED;
    }
    if (!memory_search.IsActive()) {
        return SEARCH_NO_SESSION;
    }
    return static_cast<jlong>(memory_search.Candidates().size());
}

JNIEXPORT jlong JNICALL
Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_startMemorySearch(
    JNIEnv*, jclass, jint raw_value_size, jlong raw_value) {
    const jlong availability = CheckSearchAvailability();
    if (availability != 0) {
        return availability;
    }
    const auto value_size = ToValueSize(raw_value_size);
    if (!value_size.has_value() || raw_value < 0) {
        return SEARCH_INVALID_VALUE;
    }

    Core::System& system = Core::System::GetInstance();
    const auto page_table = system.Memory().GetCurrentPageTable();
    const std::optional<u64> title_id = GetRunningTitleId();
    if (!page_table || !title_id.has_value()) {
        return SEARCH_NO_GAME;
    }

    std::scoped_lock lock{memory_search_mutex};
    const u64 session_id = AndroidNativeState::GetEmulationSessionId();
    ClearStaleSessionState(title_id, session_id);
    if (!memory_search.Begin(*value_size, static_cast<u64>(raw_value))) {
        return SEARCH_INVALID_VALUE;
    }
    memory_search_title_id = title_id;
    memory_search_session_id = session_id;

    constexpr std::array search_ranges{
        std::pair{Memory::PROCESS_IMAGE_VADDR, Memory::PROCESS_IMAGE_VADDR_END},
        std::pair{Memory::HEAP_VADDR, Memory::HEAP_VADDR_END},
    };
    for (const auto& [range_begin, range_end] : search_ranges) {
        for (VAddr page = range_begin; page < range_end; page += Memory::CITRA_PAGE_SIZE) {
            if (!IsSearchablePage(page_table->attributes[page >> Memory::CITRA_PAGE_BITS])) {
                continue;
            }
            const u8* pointer = system.Memory().GetPointer(page);
            if (!pointer) {
                continue;
            }
            const auto result = memory_search.ScanRegion(
                page, std::span<const u8>{pointer, Memory::CITRA_PAGE_SIZE}, MAX_SEARCH_CANDIDATES);
            if (result == Cheats::MemorySearch::ScanResult::TooManyCandidates) {
                memory_search_title_id.reset();
                memory_search_session_id.reset();
                return SEARCH_TOO_MANY;
            }
            if (result != Cheats::MemorySearch::ScanResult::Success) {
                memory_search.Reset();
                memory_search_title_id.reset();
                memory_search_session_id.reset();
                return SEARCH_INVALID_VALUE;
            }
        }
    }
    return static_cast<jlong>(memory_search.Candidates().size());
}

JNIEXPORT jlong JNICALL
Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_refineMemorySearch(
    JNIEnv*, jclass, jint raw_comparison, jlong raw_value) {
    const jlong availability = CheckSearchAvailability();
    if (availability != 0) {
        return availability;
    }
    const auto comparison = ToComparison(raw_comparison);
    if (!comparison.has_value()) {
        return SEARCH_INVALID_VALUE;
    }

    Core::System& system = Core::System::GetInstance();
    const auto page_table = system.Memory().GetCurrentPageTable();
    const std::optional<u64> title_id = GetRunningTitleId();
    const u64 session_id = AndroidNativeState::GetEmulationSessionId();
    std::scoped_lock lock{memory_search_mutex};
    if (ClearStaleSessionState(title_id, session_id)) {
        return SEARCH_TITLE_CHANGED;
    }
    if (!page_table || !title_id.has_value() || !memory_search.IsActive()) {
        return SEARCH_NO_SESSION;
    }
    if (raw_value < 0) {
        return SEARCH_INVALID_VALUE;
    }
    if (*comparison == Cheats::MemorySearchComparison::Exact &&
        static_cast<u64>(raw_value) > Cheats::MemorySearch::ValueMask(memory_search.ValueSize())) {
        return SEARCH_INVALID_VALUE;
    }

    const std::optional<u64> exact_value =
        *comparison == Cheats::MemorySearchComparison::Exact
            ? std::optional<u64>{static_cast<u64>(raw_value)}
            : std::nullopt;
    return static_cast<jlong>(memory_search.Refine(
        *comparison, exact_value,
        [&system, &page_table](VAddr address, Cheats::MemorySearchValueSize value_size) {
            return ReadSearchValue(system.Memory(), page_table, address, value_size);
        }));
}

JNIEXPORT jlongArray JNICALL
Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_getMemorySearchResults(
    JNIEnv* env, jclass, jint raw_limit) {
    const std::size_t limit = raw_limit > 0 ? static_cast<std::size_t>(raw_limit) : 0;
    std::scoped_lock lock{memory_search_mutex};
    const std::optional<u64> title_id = GetRunningTitleId();
    const u64 session_id = AndroidNativeState::GetEmulationSessionId();
    ClearStaleSessionState(title_id, session_id);
    if (CheckSearchAvailability() != 0 || !memory_search.IsActive() ||
        !SearchMatchesSession(title_id, session_id)) {
        return env->NewLongArray(0);
    }
    const auto candidates = memory_search.Candidates();
    const std::size_t count = std::min(limit, candidates.size());
    const jlongArray result = env->NewLongArray(static_cast<jsize>(count * 2));
    if (!result || count == 0) {
        return result;
    }
    std::vector<jlong> values(count * 2);
    for (std::size_t index = 0; index < count; ++index) {
        values[index * 2] = static_cast<jlong>(candidates[index].address);
        values[index * 2 + 1] = static_cast<jlong>(candidates[index].value);
    }
    env->SetLongArrayRegion(result, 0, static_cast<jsize>(values.size()), values.data());
    return result;
}

JNIEXPORT jint JNICALL
Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_getMemorySearchValueSize(JNIEnv*,
                                                                                      jclass) {
    std::scoped_lock lock{memory_search_mutex};
    const std::optional<u64> title_id = GetRunningTitleId();
    const u64 session_id = AndroidNativeState::GetEmulationSessionId();
    ClearStaleSessionState(title_id, session_id);
    return CheckSearchAvailability() == 0 && memory_search.IsActive() &&
                   SearchMatchesSession(title_id, session_id)
               ? static_cast<jint>(memory_search.ValueSize())
               : 0;
}

JNIEXPORT jboolean JNICALL
Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_writeMemorySearchResult(
    JNIEnv*, jclass, jlong raw_address, jlong raw_value) {
    if (CheckSearchAvailability() != 0 || raw_address < 0 || raw_value < 0) {
        return JNI_FALSE;
    }
    Core::System& system = Core::System::GetInstance();
    const auto page_table = system.Memory().GetCurrentPageTable();
    const std::optional<u64> title_id = GetRunningTitleId();
    const u64 session_id = AndroidNativeState::GetEmulationSessionId();
    std::scoped_lock lock{memory_search_mutex};
    ClearStaleSessionState(title_id, session_id);
    if (!page_table || !title_id.has_value() || !SearchMatchesSession(title_id, session_id) ||
        !memory_search.IsActive() || undo_write.has_value()) {
        return JNI_FALSE;
    }
    const VAddr address = static_cast<VAddr>(raw_address);
    const auto candidates = memory_search.Candidates();
    if (std::none_of(candidates.begin(), candidates.end(), [address](const auto& candidate) {
            return candidate.address == address;
        })) {
        return JNI_FALSE;
    }
    const auto value_size = memory_search.ValueSize();
    const u64 value = static_cast<u64>(raw_value);
    if (value > Cheats::MemorySearch::ValueMask(value_size)) {
        return JNI_FALSE;
    }
    const std::optional<u64> original =
        ReadSearchValue(system.Memory(), page_table, address, value_size);
    if (!original.has_value()) {
        return JNI_FALSE;
    }
    if (!WriteSearchValue(system, address, value_size, value)) {
        WriteSearchValue(system, address, value_size, *original);
        return JNI_FALSE;
    }
    undo_write = UndoWrite{.title_id = *title_id,
                           .session_id = session_id,
                           .address = address,
                           .value_size = value_size,
                           .original_value = *original,
                           .written_value = value};
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_canUndoMemorySearchWrite(JNIEnv*,
                                                                                     jclass) {
    std::scoped_lock lock{memory_search_mutex};
    const std::optional<u64> title_id = GetRunningTitleId();
    const u64 session_id = AndroidNativeState::GetEmulationSessionId();
    ClearStaleSessionState(title_id, session_id);
    return CheckSearchAvailability() == 0 && SearchMatchesSession(title_id, session_id) &&
                   undo_write.has_value() && undo_write->session_id == session_id
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_undoMemorySearchWrite(JNIEnv*,
                                                                                  jclass) {
    if (CheckSearchAvailability() != 0) {
        return -1;
    }
    Core::System& system = Core::System::GetInstance();
    const auto page_table = system.Memory().GetCurrentPageTable();
    const std::optional<u64> title_id = GetRunningTitleId();
    const u64 session_id = AndroidNativeState::GetEmulationSessionId();
    std::scoped_lock lock{memory_search_mutex};
    ClearStaleSessionState(title_id, session_id);
    if (!undo_write.has_value() || !page_table || !title_id.has_value() ||
        undo_write->title_id != *title_id || undo_write->session_id != session_id) {
        return 0;
    }
    const std::optional<u64> current = ReadSearchValue(
        system.Memory(), page_table, undo_write->address, undo_write->value_size);
    if (!current.has_value() || *current != undo_write->written_value) {
        return -1;
    }
    if (!WriteSearchValue(system, undo_write->address, undo_write->value_size,
                          undo_write->original_value)) {
        return -1;
    }
    undo_write.reset();
    return 1;
}

JNIEXPORT void JNICALL
Java_org_citra_citra_1emu_features_cheats_model_CheatEngine_resetMemorySearch(JNIEnv*, jclass) {
    std::scoped_lock lock{memory_search_mutex};
    if (undo_write.has_value()) {
        return;
    }
    memory_search.Reset();
    memory_search_title_id.reset();
    memory_search_session_id.reset();
}
}
