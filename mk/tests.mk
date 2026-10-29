# Each test binary is its test source linked against libnetscan.a (all backend
# objects except main, see Makefile). The linker only pulls in the archive
# members a test needs, so there are no per-test object lists to maintain.
# A new test/src/<area>/<name>_test.cpp only has to be added to one list below.

TEST_BIN_DIR = build/test/bin
TEST_SUPPORT_SRC = test/src/service/scan_service_test_support.cpp
TEST_SUPPORT_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(TEST_SUPPORT_SRC))
TEST_SUPPORT_LIB = build/test/libnetscan_test_support.a

# Unit tests without DB, HTTP server or nmap processes
FAST_TESTS = nmap_command_builder \
			 process_renderer \
			 nmap_xml_parser \
			 logger \
			 startup_validation \
			 nmap_check \
			 scan_chunk_runner \
			 target_validation \
			 presence_check_runner \
			 api_guard \
			 secret_utils \
			 config

# DB, HTTP, runtime and diff coverage
INTEGRATION_TESTS = nmap_runner \
					profile_service \
					scan_diff_model \
					scan_abort_service \
					scan_completion \
					host_service_detail \
					settings_service \
					topology_service \
					scheduler_service \
					scan_detail_route \
					scan_start_route \
					scan_abort_route \
					settings_route \
					scan_repository \
					presence_repository \
					host_meta_repository \
					allowed_target_repository \
					presence_service \
					logger_integration

TEST_SRCS = $(filter-out $(TEST_SUPPORT_SRC),$(wildcard test/src/*/*_test.cpp))
TEST_NAMES = $(patsubst %_test,%,$(notdir $(basename $(TEST_SRCS))))

UNLISTED_TESTS = $(filter-out $(FAST_TESTS) $(INTEGRATION_TESTS),$(TEST_NAMES))
ifneq ($(strip $(UNLISTED_TESTS)),)
$(error Add these tests to FAST_TESTS or INTEGRATION_TESTS in mk/tests.mk: $(UNLISTED_TESTS))
endif

test_bin = $(addprefix $(TEST_BIN_DIR)/,$(addsuffix _test,$(1)))

FAST_TEST_TARGETS = $(call test_bin,$(FAST_TESTS))
INTEGRATION_TEST_TARGETS = $(call test_bin,$(INTEGRATION_TESTS))
TEST_TARGETS = $(FAST_TEST_TARGETS) $(INTEGRATION_TEST_TARGETS)

TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(TEST_SRCS))
TEST_DEPS = $(TEST_OBJ:.o=.d) $(TEST_SUPPORT_OBJ:.o=.d)

define TEST_BINARY_RULE
$(call test_bin,$(1)): $(patsubst test/src/%.cpp,build/test/src/%.o,$(filter %/$(1)_test.cpp,$(TEST_SRCS)))
endef
$(foreach test,$(TEST_NAMES),$(eval $(call TEST_BINARY_RULE,$(test))))

$(TEST_SUPPORT_LIB): $(TEST_SUPPORT_OBJ)
	mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $^

$(TEST_BIN_DIR)/%_test: $(TEST_SUPPORT_LIB) $(NETSCAN_LIB)
	mkdir -p $(dir $@)
	$(CXX) $(TEST_CXXFLAGS) $(filter %.o,$^) $(TEST_SUPPORT_LIB) $(NETSCAN_LIB) -o $@ $(LDFLAGS) $(LDLIBS)

define RUN_TEST_BINARIES
	@set -e; for test_bin in $(1); do ./$$test_bin; done
endef

# Test target policy:
# - test/test-fast: quick local feedback.
# - test-integration: slower DB, HTTP, runtime, and diff coverage.
# - test-all: full regression suite used by CI.
.PHONY: test-fast-build test-fast-run test-fast \
		test-integration-build test-integration-run test-integration \
		test-all-build test-all-run test-all test-build test-run test test-parallel

test-fast-build: $(FAST_TEST_TARGETS)

test-fast-run: test-fast-build
	$(call RUN_TEST_BINARIES,$(FAST_TEST_TARGETS))

test-fast: test-fast-build test-fast-run

test-integration-build: $(INTEGRATION_TEST_TARGETS)

test-integration-run: test-integration-build
	$(call RUN_TEST_BINARIES,$(INTEGRATION_TEST_TARGETS))

test-integration: test-integration-build test-integration-run

test-all-build: $(TEST_TARGETS)

test-all-run: test-all-build
	$(call RUN_TEST_BINARIES,$(TEST_TARGETS))

test-all: test-all-build test-all-run

# Backward-compatible aliases
test-build: test-all-build

test-run: test-all-run

# Run individual test targets
run-%: $(TEST_BIN_DIR)/%_test
	./$<

# Parallel test execution - runs all independent tests concurrently
test-parallel: test-all-build
	@echo "Running tests in parallel..."
	@$(MAKE) -j $(TEST_TARGETS:$(TEST_BIN_DIR)/%_test=run-%) --no-print-directory

# Local default: fast tests only. Keep the integration suite explicit so local
# feedback stays quick, but do not let make test hide the full regression target.
test: test-fast-build
	@printf '%s\n' \
		"make test runs fast tests only." \
		"Use make test-all for full regression, or make test-integration for integration coverage."
	$(call RUN_TEST_BINARIES,$(FAST_TEST_TARGETS))
