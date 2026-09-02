#include "Settings.h"

#if !defined(_WIN32)
#	include <cerrno>
#	include <sys/inotify.h>
#	include <unistd.h>
#endif

namespace Settings
{
	void Main::Load() noexcept
	{
		WriteLocker locker(Lock);

	    static std::once_flag ConfigInit;
		std::call_once(ConfigInit, [&]() {
			config.Bind(UnlockedPitchInitialValue, 25.f);
			config.Bind(UnlockedPitchClampSpeed, 5.f);
			config.Bind(UnlockedPitchLimitClipping, true);
			config.Bind(UnlockedPitchFloorOffset, 0.1f);
			config.Bind(ResetZoomOnZoneChange, false);

		    config.Bind(WatchForConfigChanges, true);


			config.Bind(ExplorationUnlockPitch, true);
			config.Bind(ExplorationKeepTacticalPitchLocked, false);
			config.Bind<-89.0, 89.0>(ExplorationUnlockedPitchMin, -85.f);
			config.Bind<-89.0, 89.0>(ExplorationUnlockedPitchMax, 85.f);
			config.Bind(ExplorationOverrideLockedPitch, false);
			config.Bind<-89.0, 89.0>(ExplorationLockedPitchClose, 19.05f);          // 19.05
			config.Bind<-89.0, 89.0>(ExplorationLockedPitchFar, 40.71f);            // 40.71
			config.Bind<-89.0, 89.0>(ExplorationLockedTacticalPitchClose, 85.55f);  // 85.55
			config.Bind<-89.0, 89.0>(ExplorationLockedTacticalPitchFar, 85.55f);    // 85.55
			config.Bind<-89.0, 89.0>(ExplorationLockedAltPitchClose, 32.69f);       // 32.69
			config.Bind<-89.0, 89.0>(ExplorationLockedAltPitchFar, 39.7f);          // 39.7

			config.Bind(ExplorationOverrideZoom, true);
			config.Bind(ExplorationZoomMin, 0.5f);          // 3.5
			config.Bind(ExplorationZoomMax, 20.f);          // 12
			config.Bind(ExplorationTacticalZoomMin, 10.f);  // 10
			config.Bind(ExplorationTacticalZoomMax, 50.f);  // 50
			config.Bind(ExplorationAltZoomMin, 10.f);       // 10
			config.Bind(ExplorationAltZoomMax, 40.f);       // 40

			config.Bind(ExplorationOverrideFOV, false);
			config.Bind(ExplorationFOVClose, 55.f);     // 55
			config.Bind(ExplorationFOVFar, 55.f);       // 55
			config.Bind(ExplorationTacticalFOV, 25.f);  // 25
			config.Bind(ExplorationAltFOVClose, 45.f);  // 45
			config.Bind(ExplorationAltFOVFar, 45.f);    // 45

			config.Bind(ExplorationOverrideOffset, false);
			config.Bind(ExplorationHorizontalOffsetMult, 0.f);  // 0
			config.Bind(ExplorationVerticalOffsetMult, 0.8f);   // 0.8


			config.Bind(CombatUnlockPitch, true);
			config.Bind(CombatKeepTacticalPitchLocked, false);
			config.Bind<-89.0, 89.0>(CombatUnlockedPitchMin, -85.f);
			config.Bind<-89.0, 89.0>(CombatUnlockedPitchMax, 85.f);
			config.Bind(CombatOverrideLockedPitch, false);
			config.Bind<-89.0, 89.0>(CombatLockedPitchClose, 32.73f);          // 32.73
			config.Bind<-89.0, 89.0>(CombatLockedPitchFar, 52.42f);            // 52.42
			config.Bind<-89.0, 89.0>(CombatLockedTacticalPitchClose, 85.55f);  // 85.55
			config.Bind<-89.0, 89.0>(CombatLockedTacticalPitchFar, 85.55f);    // 85.55
			config.Bind<-89.0, 89.0>(CombatLockedAltPitchClose, 32.69f);       // 32.69
			config.Bind<-89.0, 89.0>(CombatLockedAltPitchFar, 39.7f);          // 39.7

			config.Bind(CombatOverrideZoom, true);
			config.Bind(CombatZoomMin, 0.5f);          // 4
			config.Bind(CombatZoomMax, 20.f);          // 15
			config.Bind(CombatTacticalZoomMin, 10.f);  // 10
			config.Bind(CombatTacticalZoomMax, 50.f);  // 50
			config.Bind(CombatAltZoomMin, 10.f);       // 10
			config.Bind(CombatAltZoomMax, 40.f);       // 40

			config.Bind(CombatOverrideFOV, false);
			config.Bind(CombatFOVClose, 55.f);     // 55
			config.Bind(CombatFOVFar, 55.f);       // 55
			config.Bind(CombatTacticalFOV, 25.f);  // 25
			config.Bind(CombatAltFOVClose, 45.f);  // 45
			config.Bind(CombatAltFOVFar, 45.f);    // 45

			config.Bind(CombatOverrideOffset, false);
			config.Bind(CombatHorizontalOffsetMult, 0.f);  // 0
			config.Bind(CombatVerticalOffsetMult, 0.8f);   // 0.8


			config.Bind(MouseCameraRotationMult, 1.f);
			config.Bind(MousePitchMult, 0.25f);
			config.Bind(MouseZoomMult, 0.5f);
			config.Bind(InvertMousePitch, false);

			config.Bind(KeyboardCameraRotationMult, 2.f);

			config.Bind(ControllerCameraRotationMult, 2.f);
			config.Bind(ControllerPitchMult, 0.5f);
			config.Bind(ControllerZoomMult, 0.5f);
			config.Bind(InvertControllerPitch, false);
			config.Bind(SwapZoomAndPitch, false);
			config.Bind(UseRightStickPressForZoom, false);

			config.Bind(OverrideRightStickDeadzone, true);
			config.Bind(NewDeadzone, 0.15f);
		});

		config.Load();

		INFO("Config loaded."sv)

		bChanged = true;
	}

	// from https://github.com/emoose/DLSSTweaks
    void Main::WatchForChanges()
	{
#if !defined(_WIN32)
		// inotify port of the Windows ReadDirectoryChangesW watcher.
		const auto path = std::filesystem::current_path() / CONFIG_PATH;
		const auto cfgFilename = path.filename().string();
		const auto cfgFolder = path.parent_path().string();

		int fd = ::inotify_init1(IN_CLOEXEC);
		if (fd < 0) {
			WARN("Config monitoring: inotify_init1 failed (errno {})", errno);
			return;
		}
		int wd = ::inotify_add_watch(fd, cfgFolder.c_str(),
			IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
		if (wd < 0) {
			WARN("Config monitoring: inotify_add_watch \"{}\" failed (errno {})", cfgFolder, errno);
			::close(fd);
			return;
		}

		INFO("Config monitoring: watching for config updates...");

		alignas(inotify_event) char buf[4096];
		for (;;) {
			ssize_t len = ::read(fd, buf, sizeof(buf));
			if (len <= 0) {
				if (errno == EINTR) continue;
				break;
			}
			for (char* p = buf; p < buf + len;) {
				auto* evt = reinterpret_cast<inotify_event*>(p);
				if (evt->len > 0 && cfgFilename == evt->name) {
					INFO("Config monitoring: Change detected! Updating config...")
					Load();
				}
				p += sizeof(inotify_event) + evt->len;
			}
		}

		::inotify_rm_watch(fd, wd);
		::close(fd);
#else
		const auto path = std::filesystem::current_path() / CONFIG_PATH;

		const auto cfgFilename = path.filename().wstring();
		const auto cfgFolder = path.parent_path().wstring();

		HANDLE file = CreateFileW(cfgFolder.c_str(),
			FILE_LIST_DIRECTORY,
			FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE,
			NULL,
			OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
			NULL);

		if (!file) {
			DWORD err = GetLastError();
			WARN("Config monitoring: CreateFileW \"{}\" failed with error code {}", path.parent_path().string(), err);
			return;
		}

		OVERLAPPED overlapped;
		overlapped.hEvent = CreateEvent(NULL, FALSE, 0, NULL);
		if (!overlapped.hEvent) {
			DWORD err = GetLastError();
			WARN("Config monitoring: CreateEvent failed with error code {}", err);
			CloseHandle(file);
		}

		uint8_t change_buf[1024];
		BOOL success = ReadDirectoryChangesW(
			file, change_buf, 1024, TRUE,
			FILE_NOTIFY_CHANGE_FILE_NAME |
				FILE_NOTIFY_CHANGE_DIR_NAME |
				FILE_NOTIFY_CHANGE_LAST_WRITE,
			NULL, &overlapped, NULL);

	    if (!success) {
			DWORD err = GetLastError();
			WARN("Config monitoring: ReadDirectoryChangesW failed with error code {}", err);
			return;
		}

		INFO("Config monitoring: watching for config updates...");

		while (success) {
			DWORD result = WaitForSingleObject(overlapped.hEvent, INFINITE);
			if (result == WAIT_OBJECT_0) {
				DWORD bytes_transferred;
				GetOverlappedResult(file, &overlapped, &bytes_transferred, FALSE);

				auto* evt = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(change_buf);

				for (;;) {
					if (evt->Action == FILE_ACTION_MODIFIED) {
						// evt->FileName isn't null-terminated, so construct wstring for it based on FileNameLength
						std::wstring name = std::wstring(evt->FileName, evt->FileNameLength / sizeof(WCHAR));
						if (!_wcsicmp(name.c_str(), cfgFilename.c_str())) {
							// Config read might fail if it's still being updated by a text editor etc
							// so try attempting a few times, Sleep(1000) between attempts should hopefully let us read it fine
							int attempts = 3;
							while (attempts--) {
								INFO("Config monitoring: Change detected! Updating config...")
								Load();
								break;

								Sleep(1000);
							}
						}
					}

					// Any more events to handle?
					if (evt->NextEntryOffset)
						*(uint8_t**)&evt += evt->NextEntryOffset;
					else
						break;
				}

				// Queue the next event
				success = ReadDirectoryChangesW(
					file, change_buf, 1024, TRUE,
					FILE_NOTIFY_CHANGE_FILE_NAME |
						FILE_NOTIFY_CHANGE_DIR_NAME |
						FILE_NOTIFY_CHANGE_LAST_WRITE,
					NULL, &overlapped, NULL);
			}
		}

		CloseHandle(file);
#endif
	}
}
