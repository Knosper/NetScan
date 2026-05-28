NAME = netscan

CC = gcc
CXX = g++
WINDRES = windres

UNAME_S := $(shell uname -s)
.DEFAULT_GOAL := all

CPPFLAGS = -I. -Iinclude -Ithird_party -Ithird_party/sqlite3 -Ithird_party/tinyxml2 -DCPPHTTPLIB_OPENSSL_SUPPORT
COMMON_WARNINGS = -Wall -Wextra -Werror
DEPFLAGS = -MMD -MP
WINDOWS_DEFINES = -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00

CFLAGS = $(COMMON_WARNINGS) $(DEPFLAGS)
CXXFLAGS = $(COMMON_WARNINGS) $(DEPFLAGS) -std=c++14 -Wno-deprecated-declarations
TEST_CXXFLAGS = $(CXXFLAGS) -Itest/include
SQLITE_CFLAGS = $(DEPFLAGS) $(CPPFLAGS) \
		-Wno-unused-parameter -Wno-unused-but-set-variable

LDLIBS =
LDFLAGS =

TARGET = $(NAME)
CONF_FILE = conf.ini
RELEASE_DIR = release
FRONTEND_DIR = resources/web
FRONTEND_STAMP = build/frontend.stamp
FRONTEND_INSTALL_STAMP = build/frontend-install.stamp
FRONTEND_RUNTIME_FILES = index.html app.js style.css favicon.ico
FRONTEND_GENERATED_FILES = $(FRONTEND_DIR)/app.js $(FRONTEND_DIR)/style.css

FRONTEND_DEPS = \
		$(FRONTEND_DIR)/package.json \
		$(FRONTEND_DIR)/package-lock.json

FRONTEND_SOURCES := $(shell find $(FRONTEND_DIR)/src -type f) \
		$(FRONTEND_DIR)/build.mjs \
		$(FRONTEND_DIR)/index.html

SRC =	src/main.cpp \
		src/app/application_bootstrap.cpp \
		src/app/db_bootstrap.cpp \
		src/app/service_wiring.cpp \
		src/app/http_bootstrap.cpp \
		src/app/setup_bind.cpp \
		src/app/setup_bootstrap.cpp \
		src/app/setup_wizard.cpp \
		src/app/setup_server.cpp \
		src/app/auth_config.cpp \
		src/app/config.cpp \
		src/app/logging_setup.cpp \
		src/app/paths.cpp \
		src/app/startup_validation.cpp \
		src/app/shutdown_controller.cpp \
		src/app/windows_launcher_config.cpp \
		src/app/windows_single_instance.cpp \
		src/db/database.cpp \
		src/db/allowed_target_repository.cpp \
		src/db/secret_repository.cpp \
		src/db/schema.cpp \
		src/db/write_transaction.cpp \
		src/db/dashboard_repository.cpp \
		src/db/host_repository.cpp \
		src/db/host_meta_repository.cpp \
		src/db/note_repository.cpp \
		src/db/presence_repository.cpp \
		src/db/schedule_repository.cpp \
		src/http/server.cpp \
		src/http/routes.cpp \
		src/http/route_utils.cpp \
		src/http/static_files.cpp \
		src/http/api_health.cpp \
		src/http/api_health_route.cpp \
		src/http/api_dashboard.cpp \
		src/http/api_guard.cpp \
		src/http/api_host.cpp \
		src/http/api_notes.cpp \
		src/http/api_presence.cpp \
		src/http/api_profiles.cpp \
		src/http/api_scan.cpp \
		src/http/scan_response_builders.cpp \
		src/http/api_scheduler.cpp \
		src/http/api_settings.cpp \
		src/http/api_setup_reset.cpp \
		src/http/api_static.cpp \
		src/http/api_topology.cpp \
		src/http/responses.cpp \
		src/service/dashboard_service.cpp \
		src/service/health_service.cpp \
		src/service/host_service.cpp \
		src/service/settings_service.cpp \
		src/service/note_service.cpp \
		src/service/presence_service.cpp \
		src/service/presence_check_runner.cpp \
		src/service/presence_scheduler_service.cpp \
		src/service/scan_service.cpp \
		src/service/scan_diff_service.cpp \
		src/service/scan_history_service.cpp \
		src/service/topology_service.cpp \
		src/service/scheduler_service.cpp \
		src/service/scan_service_runtime.cpp \
		src/service/scan_persistence.cpp \
		src/service/scan_completion.cpp \
		src/service/scan_result_builders.cpp \
		src/service/scan_messages.cpp \
		src/scan/target_validation.cpp \
		src/db/scan_repository.cpp \
		src/db/scan_host_repository.cpp \
		src/db/scan_port_repository.cpp \
		src/db/scan_coverage_repository.cpp \
		src/db/scan_diff_repository.cpp \
		src/scan/nmap.cpp \
		src/util/path_utils.cpp \
		src/util/logger.cpp \
		src/util/file_permissions.cpp \
		src/util/secret_utils.cpp \
		src/util/string_utils.cpp \
		src/util/time_utils.cpp \
		src/scan/nmap_command_builder.cpp \
		src/scan/scan_chunk_planner.cpp \
		src/scan/scan_chunk_runner.cpp \
		src/scan/scan_snapshot_merger.cpp \
		src/scan/port_spec_validator.cpp \
		src/scan/process_renderer.cpp \
		src/scan/nmap_runner_internal.cpp \
		src/scan/nmap_runner_common.cpp \
		src/scan/scan_orchestrator.cpp \
		src/scan/nmap_xml_parser.cpp \
		src/db/profile_repository.cpp \
		src/service/profile_service.cpp

SQLITE_SRC = third_party/sqlite3/sqlite3.c
SQLITE_OBJ = build/third_party/sqlite3/sqlite3.o
SQLITE_DEP = build/third_party/sqlite3/sqlite3.d

TINYXML2_DIR = third_party/tinyxml2
TINYXML2_SRC = $(TINYXML2_DIR)/tinyxml2.cpp
TINYXML2_OBJ = build/third_party/tinyxml2/tinyxml2.o

OBJ = $(SRC:src/%.cpp=build/%.o)

LAUNCHER_SRC = src/app/windows_launcher.cpp
LAUNCHER_OBJ = build/app/windows_launcher.o
LAUNCHER_SUPPORT_OBJ = build/app/paths.o build/app/windows_launcher_config.o build/app/windows_single_instance.o build/util/path_utils.o
LAUNCHER_RES = resources/launcher.rc
LAUNCHER_RES_OBJ =
LAUNCHER_TARGET =

RES =
RES_OBJ =
DLLS =

ifneq (,$(filter MINGW% MSYS% CYGWIN%,$(UNAME_S)))
	CPPFLAGS += $(WINDOWS_DEFINES)
	CXXFLAGS += -Wno-conversion-null
	LDFLAGS += -static -static-libgcc -static-libstdc++
	# OpenSSL: requires mingw-w64-x86_64-openssl (MSYS2) at build time; DLLs must ship with the binary
	# Order matters: -lssl -lcrypto must precede -lws2_32/-lcrypt32 so the linker resolves their winsock/cert deps
	LDLIBS += -lssl -lcrypto -lws2_32 -lbcrypt -lcrypt32 -lgdi32 -ladvapi32
	TARGET = $(NAME).exe
	RES = resources/app.rc
	RES_OBJ = build/appres.o
	LAUNCHER_TARGET = netscan-launcher.exe
	LAUNCHER_RES_OBJ = build/launcherres.o
	NMAP_RUNNER_PLATFORM_SRC = src/scan/nmap_runner_win32.cpp
else
	# Link OpenSSL statically, keep glibc dynamic (AppImage runtime handles it)
	LDLIBS += -Wl,-Bstatic -lssl -lcrypto -Wl,-Bdynamic
	LDFLAGS += -static-libgcc -static-libstdc++
	NMAP_RUNNER_PLATFORM_SRC = src/scan/nmap_runner_posix.cpp
endif

SRC += $(NMAP_RUNNER_PLATFORM_SRC)

include mk/tests.mk

all: $(FRONTEND_STAMP) $(TARGET) $(CONF_FILE)
backend: $(TARGET) $(CONF_FILE)
frontend: $(FRONTEND_STAMP)

$(FRONTEND_INSTALL_STAMP): $(FRONTEND_DEPS)
	mkdir -p $(dir $@)
	cd $(FRONTEND_DIR) && npm ci
	touch $@

$(FRONTEND_STAMP): $(FRONTEND_INSTALL_STAMP) $(FRONTEND_SOURCES)
	mkdir -p $(dir $@)
	cd $(FRONTEND_DIR) && npm run build
	touch $@

$(TARGET): $(OBJ) $(SQLITE_OBJ) $(TINYXML2_OBJ) $(RES_OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) $(SQLITE_OBJ) $(TINYXML2_OBJ) $(RES_OBJ) -o $(TARGET) $(LDFLAGS) $(LDLIBS)

# Canonical default conf.ini content — single source of truth.
# Used both for generating a local dev conf.ini and for shipping in release packages.
define DEFAULT_CONF_CONTENT
host=127.0.0.1
port=8080
db_path=./netscan.db
web_dir=./resources/web
log_level=info
# log_file=./netscan.log
ui_enabled=true
scan_cooldown_seconds=0
tls_enabled=false
# tls_cert_path=./certs/netscan.crt
# tls_key_path=./certs/netscan.key
endef
export DEFAULT_CONF_CONTENT

$(CONF_FILE):
	@if [ ! -f $(CONF_FILE) ]; then \
		echo "Creating default $(CONF_FILE)"; \
		printf '%s\n' "$$DEFAULT_CONF_CONTENT" > $(CONF_FILE); \
	fi

build/%.o: src/%.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/test/src/%.o: test/src/%.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(TEST_CXXFLAGS) -c $< -o $@

$(SQLITE_OBJ): $(SQLITE_SRC)
	mkdir -p $(dir $@)
	$(CC) $(SQLITE_CFLAGS) -c $< -o $@

$(TINYXML2_OBJ): $(TINYXML2_SRC)
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Wno-unused-parameter -c $< -o $@

build/appres.o: $(RES)
	mkdir -p build
	$(WINDRES) $(RES) -O coff -o $@

APPIMAGE_TOOL = build/appimagetool-x86_64.AppImage
APPIMAGE_TOOL_URL = https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
APPDIR_STAGING = build/AppDir

define COPY_RELEASE_CONF
	printf '%s\n' "$$DEFAULT_CONF_CONTENT" > $(1)/conf.ini
endef

define COPY_RELEASE_WEB
	mkdir -p $(1)/resources/web
	for file in $(FRONTEND_RUNTIME_FILES); do cp $(FRONTEND_DIR)/$$file $(1)/resources/web/; done
endef

# Download appimagetool if not present
$(APPIMAGE_TOOL):
	mkdir -p $(dir $@)
	curl -fsSL -o $@ $(APPIMAGE_TOOL_URL)
	chmod +x $@

# Build an AppImage.
# $(1) = AppDir staging path, $(2) = output filename, $(3) = binary, $(4) = with-ui (1) or api-only (0)
define BUILD_APPIMAGE
	rm -rf $(1)
	mkdir -p $(1)/usr/bin $(1)/usr/share/applications $(1)/usr/share/icons/hicolor/256x256/apps
	cp $(3) $(1)/usr/bin/netscan
	printf '#!/bin/sh\nHERE="$$(dirname "$$(readlink -f "$$0")")"\nEX_TEMPFAIL=75\nwhile true; do\n  "$$HERE/usr/bin/netscan" "$$@"\n  code=$$?\n  [ $$code -eq $$EX_TEMPFAIL ] || exit $$code\ndone\n' > $(1)/AppRun
	chmod +x $(1)/AppRun
	cp resources/app.ico $(1)/usr/share/icons/hicolor/256x256/apps/netscan.png 2>/dev/null || true
	printf '[Desktop Entry]\nName=NetScan\nExec=netscan\nIcon=netscan\nType=Application\nCategories=Network;\n' > $(1)/usr/share/applications/netscan.desktop
	ln -sf usr/share/applications/netscan.desktop $(1)/netscan.desktop
	ln -sf usr/share/icons/hicolor/256x256/apps/netscan.png $(1)/netscan.png 2>/dev/null || true
	$(if $(filter 1,$(4)),$(call COPY_RELEASE_WEB,$(1)/usr/bin))
	ARCH=x86_64 $(APPIMAGE_TOOL) --appimage-extract-and-run --no-appstream $(1) $(RELEASE_DIR)/$(2)
endef

ifneq (,$(filter MINGW% MSYS% CYGWIN%,$(UNAME_S)))

$(LAUNCHER_TARGET): $(LAUNCHER_OBJ) $(LAUNCHER_SUPPORT_OBJ) $(LAUNCHER_RES_OBJ)
	$(CXX) $(CXXFLAGS) -mwindows $(LAUNCHER_OBJ) $(LAUNCHER_SUPPORT_OBJ) $(LAUNCHER_RES_OBJ) -o $(LAUNCHER_TARGET) $(LDFLAGS) -lws2_32 -lshell32

build/launcherres.o: $(LAUNCHER_RES)
	mkdir -p build
	$(WINDRES) $(LAUNCHER_RES) -O coff -o $@

release-linux:
	@echo "ERROR: release-linux is not supported on this platform ($(UNAME_S)). Use 'make release-windows' instead." >&2; exit 1

release-windows: $(FRONTEND_STAMP) $(NAME).exe $(LAUNCHER_TARGET)
	rm -rf $(RELEASE_DIR)
	mkdir -p $(RELEASE_DIR)
	cp $(NAME).exe $(RELEASE_DIR)/netscan-server.exe
	cp $(LAUNCHER_TARGET) $(RELEASE_DIR)/NetScan.exe
	cp scripts/stop.bat $(RELEASE_DIR)/stop.bat
	$(call COPY_RELEASE_CONF,$(RELEASE_DIR))
	$(call COPY_RELEASE_WEB,$(RELEASE_DIR))

release-api: $(NAME).exe
	rm -rf $(RELEASE_DIR)
	mkdir -p $(RELEASE_DIR)
	cp $(NAME).exe $(RELEASE_DIR)/netscan-api.exe

release-ui: release-windows

release: release-windows

else

release-windows:
	@echo "ERROR: release-windows is not supported on this platform ($(UNAME_S)). Use 'make release-linux' instead." >&2; exit 1

release-linux: $(FRONTEND_STAMP) $(NAME) $(APPIMAGE_TOOL)
	rm -rf $(RELEASE_DIR)
	mkdir -p $(RELEASE_DIR)
	cp scripts/stop.sh $(RELEASE_DIR)/stop.sh
	chmod +x $(RELEASE_DIR)/stop.sh
	$(call BUILD_APPIMAGE,$(APPDIR_STAGING)/ui,netscan-linux-ui.AppImage,$(NAME),1)

release-api: $(NAME) $(APPIMAGE_TOOL)
	rm -rf $(RELEASE_DIR)
	mkdir -p $(RELEASE_DIR)
	$(call BUILD_APPIMAGE,$(APPDIR_STAGING)/api,netscan-linux-api.AppImage,$(NAME),0)

release-ui: release-linux

release: release-linux

endif

clean:
	rm -rf build
	rm -f $(FRONTEND_GENERATED_FILES)

fclean: clean
	rm -f $(TARGET)
	rm -f $(LAUNCHER_TARGET)
	rm -rf $(RELEASE_DIR)
	rm -f netscan.db


re: fclean all

# ── timing ─────────────────────────────────────────────

TIME_BEFORE := $(shell date +%s%3N)

define time_goal
	@$(eval TIME_START := $(shell date +%s%3N))
	@+$(MAKE) --no-print-directory $1
	@$(eval TIME_END := $(shell date +%s%3N))
	@echo "⏱️  $1: $$(( ($(TIME_END) - $(TIME_START)) / 1000 )).$$(printf %03d $$(( ($(TIME_END) - $(TIME_START)) % 1000 )))s"
endef

time-all:
	$(call time_goal,all)

time-backend:
	$(call time_goal,backend)

time-test-build:
	$(call time_goal,test-build)

time-test:
	$(call time_goal,test)

time-full:
	@$(eval TOTAL_START := $(TIME_BEFORE))
	@echo "==== Full build timing ===="
	+$(MAKE) --no-print-directory clean
	+$(MAKE) --no-print-directory time-all time-test-build time-test
	@$(eval TOTAL_END := $(shell date +%s%3N))
	@echo "=========================="
	@echo "⏱️  TOTAL: $$(( ($(TOTAL_END) - $(TOTAL_START)) / 1000 )).$$(printf %03d $$(( ($(TOTAL_END) - $(TOTAL_START)) % 1000 )))s"

print:
	@echo UNAME_S=$(UNAME_S)
	@echo TARGET=$(TARGET)
	@echo CONF_FILE=$(CONF_FILE)
	@echo CPPFLAGS=$(CPPFLAGS)
	@echo CFLAGS=$(CFLAGS)
	@echo CXXFLAGS=$(CXXFLAGS)
	@echo SQLITE_CFLAGS=$(SQLITE_CFLAGS)
	@echo LDFLAGS=$(LDFLAGS)
	@echo LDLIBS=$(LDLIBS)
	@echo RES=$(RES)
	@echo RES_OBJ=$(RES_OBJ)
	@echo SQLITE_OBJ=$(SQLITE_OBJ)

-include $(OBJ:.o=.d) $(SQLITE_DEP) $(TINYXML2_OBJ:.o=.d) $(TEST_DEPS)

.PHONY: all backend frontend clean fclean re print release release-linux release-windows release-api release-ui test time-all time-backend time-test-build time-test time-full
