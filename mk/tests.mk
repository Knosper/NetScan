TEST_BIN_DIR = build/test/bin
TEST_SUPPORT_SRC = test/src/service/scan_service_test_support.cpp
TEST_SUPPORT_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(TEST_SUPPORT_SRC))
TEST_SUPPORT_DEP = $(TEST_SUPPORT_OBJ:.o=.d)

SCAN_REPOSITORY_SRC = src/db/scan_repository.cpp \
					  src/db/scan_host_repository.cpp \
					  src/db/scan_port_repository.cpp \
					  src/db/scan_coverage_repository.cpp \
					  src/db/scan_diff_repository.cpp

# Einzeltests
TEST_SRC = test/src/scan/nmap_command_builder_test.cpp \
		   src/scan/nmap_command_builder.cpp \
		   src/scan/scan_chunk_planner.cpp \
		   src/scan/target_validation.cpp \
		   src/scan/port_spec_validator.cpp \
		   src/util/string_utils.cpp

TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(TEST_SRC)))
TEST_DEP = $(TEST_OBJ:.o=.d)
TEST_TARGET = $(TEST_BIN_DIR)/nmap_command_builder_test

RUNNER_TEST_SRC = test/src/scan/nmap_runner_test.cpp \
				  src/scan/nmap_runner_internal.cpp \
				  src/scan/nmap_runner_common.cpp \
				  $(NMAP_RUNNER_PLATFORM_SRC) \
				  src/scan/target_validation.cpp \
				  src/util/logger.cpp \
				  src/util/string_utils.cpp
RUNNER_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(RUNNER_TEST_SRC)))
RUNNER_TEST_DEP = $(RUNNER_TEST_OBJ:.o=.d)
RUNNER_TEST_TARGET = $(TEST_BIN_DIR)/nmap_runner_test

NMAP_CHECK_TEST_SRC = test/src/scan/nmap_check_test.cpp \
					  src/scan/nmap.cpp
NMAP_CHECK_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(NMAP_CHECK_TEST_SRC)))
NMAP_CHECK_TEST_DEP = $(NMAP_CHECK_TEST_OBJ:.o=.d)
NMAP_CHECK_TEST_TARGET = $(TEST_BIN_DIR)/nmap_check_test

SCAN_CHUNK_RUNNER_TEST_SRC = test/src/scan/scan_chunk_runner_test.cpp \
							 src/scan/scan_chunk_runner.cpp \
							 src/scan/nmap_command_builder.cpp \
							 src/scan/nmap_runner_internal.cpp \
							 src/scan/nmap_runner_common.cpp \
							 $(NMAP_RUNNER_PLATFORM_SRC) \
							 src/scan/process_renderer.cpp \
							 src/scan/target_validation.cpp \
							 src/util/logger.cpp \
							 src/util/string_utils.cpp
SCAN_CHUNK_RUNNER_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(SCAN_CHUNK_RUNNER_TEST_SRC)))
SCAN_CHUNK_RUNNER_TEST_DEP = $(SCAN_CHUNK_RUNNER_TEST_OBJ:.o=.d)
SCAN_CHUNK_RUNNER_TEST_TARGET = $(TEST_BIN_DIR)/scan_chunk_runner_test

SCAN_DIFF_MODEL_TEST_SRC = test/src/service/scan_diff_model_test.cpp \
						   src/service/scan_diff_service.cpp \
						   src/service/scan_persistence.cpp \
						   src/db/database.cpp \
						   src/db/schema.cpp \
						   $(SCAN_REPOSITORY_SRC) \
						   src/scan/port_spec_validator.cpp \
						   src/scan/target_validation.cpp \
						   src/util/file_permissions.cpp \
						   src/util/logger.cpp \
						   src/util/path_utils.cpp \
						   src/util/string_utils.cpp
SCAN_DIFF_MODEL_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(SCAN_DIFF_MODEL_TEST_SRC)))
SCAN_DIFF_MODEL_TEST_DEP = $(SCAN_DIFF_MODEL_TEST_OBJ:.o=.d)
SCAN_DIFF_MODEL_TEST_TARGET = $(TEST_BIN_DIR)/scan_diff_model_test

SCAN_ABORT_SERVICE_TEST_SRC = test/src/service/scan_abort_service_test.cpp \
							  src/app/auth_config.cpp \
							  src/service/scan_service.cpp \
							  src/service/scan_service_runtime.cpp \
							  src/service/scan_persistence.cpp \
							  src/service/scan_completion.cpp \
							  src/service/scan_result_builders.cpp \
							  src/service/scan_messages.cpp \
							  src/service/settings_service.cpp \
							  src/app/config.cpp \
							  src/app/paths.cpp \
							  src/util/path_utils.cpp \
							  src/util/file_permissions.cpp \
							  src/db/database.cpp \
							  src/db/allowed_target_repository.cpp \
							  src/db/secret_repository.cpp \
							  src/db/write_transaction.cpp \
							  src/db/schema.cpp \
							  $(SCAN_REPOSITORY_SRC) \
							  src/scan/nmap.cpp \
							  src/scan/nmap_command_builder.cpp \
							  src/scan/scan_chunk_planner.cpp \
							  src/scan/scan_chunk_runner.cpp \
							  src/scan/nmap_xml_parser.cpp \
							  src/scan/scan_snapshot_merger.cpp \
							  src/scan/nmap_runner_internal.cpp \
							  src/scan/nmap_runner_common.cpp \
							  $(NMAP_RUNNER_PLATFORM_SRC) \
							  src/scan/port_spec_validator.cpp \
							  src/scan/process_renderer.cpp \
							  src/scan/scan_orchestrator.cpp \
							  src/scan/target_validation.cpp \
							  src/util/logger.cpp \
							  src/util/secret_utils.cpp \
							  src/util/string_utils.cpp \
							  src/util/time_utils.cpp
SCAN_ABORT_SERVICE_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(SCAN_ABORT_SERVICE_TEST_SRC)))
SCAN_ABORT_SERVICE_TEST_DEP = $(SCAN_ABORT_SERVICE_TEST_OBJ:.o=.d)
SCAN_ABORT_SERVICE_TEST_TARGET = $(TEST_BIN_DIR)/scan_abort_service_test

SCAN_COMPLETION_TEST_SRC = test/src/service/scan_completion_test.cpp \
						   src/service/scan_completion.cpp \
						   src/service/scan_result_builders.cpp \
						   src/service/scan_persistence.cpp \
						   src/service/scan_messages.cpp \
						   src/db/database.cpp \
						   src/db/write_transaction.cpp \
						   src/db/schema.cpp \
						   $(SCAN_REPOSITORY_SRC) \
						   src/scan/nmap_xml_parser.cpp \
						   src/scan/port_spec_validator.cpp \
						   src/scan/scan_snapshot_merger.cpp \
						   src/scan/target_validation.cpp \
						   src/util/file_permissions.cpp \
						   src/util/logger.cpp \
						   src/util/path_utils.cpp \
						   src/util/string_utils.cpp
SCAN_COMPLETION_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(SCAN_COMPLETION_TEST_SRC)))
SCAN_COMPLETION_TEST_DEP = $(SCAN_COMPLETION_TEST_OBJ:.o=.d)
SCAN_COMPLETION_TEST_TARGET = $(TEST_BIN_DIR)/scan_completion_test

HOST_SERVICE_DETAIL_TEST_SRC = test/src/service/host_service_detail_test.cpp \
							   src/service/host_service.cpp \
							   src/service/scan_persistence.cpp \
							   src/db/database.cpp \
							   src/db/schema.cpp \
							   src/db/host_repository.cpp \
							   src/db/host_meta_repository.cpp \
							   $(SCAN_REPOSITORY_SRC) \
							   src/scan/port_spec_validator.cpp \
							   src/scan/target_validation.cpp \
							   src/util/file_permissions.cpp \
							   src/util/logger.cpp \
							   src/util/path_utils.cpp \
							   src/util/string_utils.cpp
HOST_SERVICE_DETAIL_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(HOST_SERVICE_DETAIL_TEST_SRC)))
HOST_SERVICE_DETAIL_TEST_DEP = $(HOST_SERVICE_DETAIL_TEST_OBJ:.o=.d)
HOST_SERVICE_DETAIL_TEST_TARGET = $(TEST_BIN_DIR)/host_service_detail_test

SETTINGS_SERVICE_TEST_SRC = test/src/service/settings_service_test.cpp \
							src/service/settings_service.cpp \
							src/app/config.cpp \
							src/app/paths.cpp \
							src/util/file_permissions.cpp \
							src/db/database.cpp \
							src/db/allowed_target_repository.cpp \
							src/db/schema.cpp \
							src/scan/target_validation.cpp \
							src/util/path_utils.cpp \
							src/util/logger.cpp \
							src/util/string_utils.cpp
SETTINGS_SERVICE_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(SETTINGS_SERVICE_TEST_SRC)))
SETTINGS_SERVICE_TEST_DEP = $(SETTINGS_SERVICE_TEST_OBJ:.o=.d)
SETTINGS_SERVICE_TEST_TARGET = $(TEST_BIN_DIR)/settings_service_test

TOPOLOGY_SERVICE_TEST_SRC = test/src/service/topology_service_test.cpp \
							   src/service/topology_service.cpp \
							   src/service/scan_persistence.cpp \
							   src/db/database.cpp \
							   src/db/schema.cpp \
							   $(SCAN_REPOSITORY_SRC) \
							   src/db/host_meta_repository.cpp \
							   src/scan/port_spec_validator.cpp \
							   src/scan/target_validation.cpp \
							   src/util/file_permissions.cpp \
							   src/util/logger.cpp \
							   src/util/path_utils.cpp \
							   src/util/string_utils.cpp
TOPOLOGY_SERVICE_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(TOPOLOGY_SERVICE_TEST_SRC)))
TOPOLOGY_SERVICE_TEST_DEP = $(TOPOLOGY_SERVICE_TEST_OBJ:.o=.d)
TOPOLOGY_SERVICE_TEST_TARGET = $(TEST_BIN_DIR)/topology_service_test

SCHEDULER_SERVICE_TEST_SRC = test/src/service/scheduler_service_test.cpp \
							 src/service/scheduler_service.cpp \
							 src/db/database.cpp \
							 src/db/schema.cpp \
							 src/db/schedule_repository.cpp \
							 src/scan/target_validation.cpp \
							 src/scan/port_spec_validator.cpp \
							 src/util/file_permissions.cpp \
							 src/util/logger.cpp \
							 src/util/path_utils.cpp \
							 src/util/time_utils.cpp \
							 src/util/string_utils.cpp
SCHEDULER_SERVICE_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(SCHEDULER_SERVICE_TEST_SRC)))
SCHEDULER_SERVICE_TEST_DEP = $(SCHEDULER_SERVICE_TEST_OBJ:.o=.d)
SCHEDULER_SERVICE_TEST_TARGET = $(TEST_BIN_DIR)/scheduler_service_test

SCAN_DETAIL_ROUTE_TEST_SRC = test/src/http/scan_detail_route_test.cpp \
							 src/app/auth_config.cpp \
							 src/app/config.cpp \
							 src/app/paths.cpp \
							 src/util/file_permissions.cpp \
							 src/app/shutdown_controller.cpp \
							 src/db/database.cpp \
							 src/db/allowed_target_repository.cpp \
							 src/db/secret_repository.cpp \
							 src/db/write_transaction.cpp \
							 src/db/schema.cpp \
							 src/db/dashboard_repository.cpp \
							 src/db/host_repository.cpp \
							 src/db/host_meta_repository.cpp \
							 $(SCAN_REPOSITORY_SRC) \
							 src/db/profile_repository.cpp \
							 src/db/schedule_repository.cpp \
							 src/http/routes.cpp \
							 src/http/route_utils.cpp \
							 src/http/static_files.cpp \
							 src/http/api_health.cpp \
							 src/http/api_health_route.cpp \
							 src/http/api_dashboard.cpp \
							 src/http/api_guard.cpp \
							 src/http/api_host.cpp \
							 src/http/api_notes.cpp \
							 src/http/api_profiles.cpp \
							 src/http/api_scan.cpp \
							 src/http/scan_response_builders.cpp \
							 src/http/api_scheduler.cpp \
							 src/http/api_settings.cpp \
							 src/http/api_setup_reset.cpp \
							 src/http/api_static.cpp \
							 src/http/responses.cpp \
							 src/service/dashboard_service.cpp \
							 src/service/health_service.cpp \
							 src/service/host_service.cpp \
							 src/service/settings_service.cpp \
							 src/db/note_repository.cpp \
							 src/service/note_service.cpp \
							 src/service/profile_service.cpp \
							 src/service/scheduler_service.cpp \
							 src/service/scan_diff_service.cpp \
							 src/service/scan_history_service.cpp \
							 src/service/scan_service.cpp \
							 src/service/scan_service_runtime.cpp \
							 src/service/scan_persistence.cpp \
							 src/service/scan_completion.cpp \
							 src/service/scan_result_builders.cpp \
							 src/service/scan_messages.cpp \
							 src/scan/nmap.cpp \
							 src/scan/nmap_command_builder.cpp \
							 src/scan/scan_chunk_planner.cpp \
							 src/scan/scan_chunk_runner.cpp \
							 src/scan/nmap_xml_parser.cpp \
							 src/scan/scan_snapshot_merger.cpp \
							 src/scan/nmap_runner_internal.cpp \
							 src/scan/nmap_runner_common.cpp \
							  $(NMAP_RUNNER_PLATFORM_SRC) \
							 src/scan/port_spec_validator.cpp \
							 src/scan/process_renderer.cpp \
							 src/scan/scan_orchestrator.cpp \
						  src/scan/target_validation.cpp \
						  src/util/path_utils.cpp \
						  src/util/logger.cpp \
						  src/util/secret_utils.cpp \
						  src/util/string_utils.cpp \
						  src/util/time_utils.cpp
SCAN_DETAIL_ROUTE_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(SCAN_DETAIL_ROUTE_TEST_SRC)))
SCAN_DETAIL_ROUTE_TEST_DEP = $(SCAN_DETAIL_ROUTE_TEST_OBJ:.o=.d)
SCAN_DETAIL_ROUTE_TEST_TARGET = $(TEST_BIN_DIR)/scan_detail_route_test

SCAN_START_ROUTE_TEST_SRC = test/src/http/scan_start_route_test.cpp \
							src/app/auth_config.cpp \
							src/app/config.cpp \
							src/app/paths.cpp \
							src/util/file_permissions.cpp \
							src/app/shutdown_controller.cpp \
							src/db/database.cpp \
							src/db/allowed_target_repository.cpp \
							src/db/secret_repository.cpp \
							src/db/write_transaction.cpp \
							src/db/schema.cpp \
							src/db/dashboard_repository.cpp \
							src/db/host_repository.cpp \
							src/db/host_meta_repository.cpp \
							$(SCAN_REPOSITORY_SRC) \
							src/db/profile_repository.cpp \
							src/db/schedule_repository.cpp \
							src/http/routes.cpp \
							src/http/route_utils.cpp \
							src/http/static_files.cpp \
							src/http/api_health.cpp \
							src/http/api_health_route.cpp \
							src/http/api_dashboard.cpp \
							src/http/api_guard.cpp \
							src/http/api_host.cpp \
							src/http/api_notes.cpp \
							src/http/api_profiles.cpp \
							src/http/api_scan.cpp \
							src/http/scan_response_builders.cpp \
							src/http/api_scheduler.cpp \
							src/http/api_settings.cpp \
							src/http/api_setup_reset.cpp \
							src/http/api_static.cpp \
							src/http/responses.cpp \
							src/service/dashboard_service.cpp \
							src/service/health_service.cpp \
							src/service/host_service.cpp \
							src/service/settings_service.cpp \
							src/db/note_repository.cpp \
							src/service/note_service.cpp \
							src/service/profile_service.cpp \
							src/service/scheduler_service.cpp \
							src/service/scan_service.cpp \
							src/service/scan_service_runtime.cpp \
							src/service/scan_persistence.cpp \
							src/service/scan_completion.cpp \
							src/service/scan_result_builders.cpp \
							src/service/scan_messages.cpp \
							src/scan/nmap.cpp \
							src/scan/nmap_command_builder.cpp \
							src/scan/scan_chunk_planner.cpp \
							src/scan/scan_chunk_runner.cpp \
							src/scan/nmap_xml_parser.cpp \
							src/scan/scan_snapshot_merger.cpp \
							src/scan/nmap_runner_internal.cpp \
							src/scan/nmap_runner_common.cpp \
							  $(NMAP_RUNNER_PLATFORM_SRC) \
							src/scan/port_spec_validator.cpp \
							src/scan/process_renderer.cpp \
							src/scan/scan_orchestrator.cpp \
							src/scan/target_validation.cpp \
							src/util/path_utils.cpp \
							src/util/logger.cpp \
							src/util/secret_utils.cpp \
							src/util/string_utils.cpp \
							src/util/time_utils.cpp
SCAN_START_ROUTE_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(SCAN_START_ROUTE_TEST_SRC)))
SCAN_START_ROUTE_TEST_DEP = $(SCAN_START_ROUTE_TEST_OBJ:.o=.d)
SCAN_START_ROUTE_TEST_TARGET = $(TEST_BIN_DIR)/scan_start_route_test

SCAN_ABORT_ROUTE_TEST_SRC = test/src/http/scan_abort_route_test.cpp \
							src/app/auth_config.cpp \
							src/app/config.cpp \
							src/app/paths.cpp \
							src/util/file_permissions.cpp \
							src/app/shutdown_controller.cpp \
							src/db/database.cpp \
							src/db/allowed_target_repository.cpp \
							src/db/secret_repository.cpp \
							src/db/write_transaction.cpp \
							src/db/schema.cpp \
							src/db/dashboard_repository.cpp \
							src/db/host_repository.cpp \
							src/db/host_meta_repository.cpp \
							$(SCAN_REPOSITORY_SRC) \
							src/db/profile_repository.cpp \
							src/db/schedule_repository.cpp \
							src/http/routes.cpp \
							src/http/route_utils.cpp \
							src/http/static_files.cpp \
							src/http/api_health.cpp \
							src/http/api_health_route.cpp \
							src/http/api_dashboard.cpp \
							src/http/api_guard.cpp \
							src/http/api_host.cpp \
							src/http/api_notes.cpp \
							src/http/api_profiles.cpp \
							src/http/api_scan.cpp \
							src/http/scan_response_builders.cpp \
							src/http/api_scheduler.cpp \
							src/http/api_settings.cpp \
							src/http/api_setup_reset.cpp \
							src/http/api_static.cpp \
							src/http/responses.cpp \
							src/service/dashboard_service.cpp \
							src/service/health_service.cpp \
							src/service/host_service.cpp \
							src/service/settings_service.cpp \
							src/db/note_repository.cpp \
							src/service/note_service.cpp \
							src/service/profile_service.cpp \
							src/service/scheduler_service.cpp \
							src/service/scan_service.cpp \
							src/service/scan_service_runtime.cpp \
							src/service/scan_persistence.cpp \
							src/service/scan_completion.cpp \
							src/service/scan_result_builders.cpp \
							src/service/scan_messages.cpp \
							src/scan/nmap.cpp \
							src/scan/nmap_command_builder.cpp \
							src/scan/scan_chunk_planner.cpp \
							src/scan/scan_chunk_runner.cpp \
							src/scan/nmap_xml_parser.cpp \
							src/scan/scan_snapshot_merger.cpp \
							src/scan/nmap_runner_internal.cpp \
							src/scan/nmap_runner_common.cpp \
							  $(NMAP_RUNNER_PLATFORM_SRC) \
							src/scan/port_spec_validator.cpp \
							src/scan/process_renderer.cpp \
							src/scan/scan_orchestrator.cpp \
							src/scan/target_validation.cpp \
							src/util/path_utils.cpp \
							src/util/logger.cpp \
							src/util/secret_utils.cpp \
							src/util/string_utils.cpp \
							src/util/time_utils.cpp
SCAN_ABORT_ROUTE_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(SCAN_ABORT_ROUTE_TEST_SRC)))
SCAN_ABORT_ROUTE_TEST_DEP = $(SCAN_ABORT_ROUTE_TEST_OBJ:.o=.d)
SCAN_ABORT_ROUTE_TEST_TARGET = $(TEST_BIN_DIR)/scan_abort_route_test

SETTINGS_ROUTE_TEST_SRC = test/src/http/settings_route_test.cpp \
						  src/app/auth_config.cpp \
						  src/app/config.cpp \
						  src/app/paths.cpp \
						  src/app/setup_bind.cpp \
						  src/util/file_permissions.cpp \
						  src/app/shutdown_controller.cpp \
						  src/db/database.cpp \
						  src/db/allowed_target_repository.cpp \
						  src/db/secret_repository.cpp \
						  src/db/write_transaction.cpp \
						  src/db/schema.cpp \
						  src/db/dashboard_repository.cpp \
						  src/db/host_repository.cpp \
						  src/db/host_meta_repository.cpp \
						  $(SCAN_REPOSITORY_SRC) \
						  src/db/profile_repository.cpp \
						  src/db/schedule_repository.cpp \
						  src/http/routes.cpp \
						  src/http/route_utils.cpp \
						  src/http/static_files.cpp \
						  src/http/api_health.cpp \
						  src/http/api_health_route.cpp \
						  src/http/api_dashboard.cpp \
						  src/http/api_guard.cpp \
						  src/http/api_host.cpp \
						  src/http/api_notes.cpp \
						  src/http/api_profiles.cpp \
						  src/http/api_scan.cpp \
						  src/http/scan_response_builders.cpp \
						  src/http/api_scheduler.cpp \
						  src/http/api_settings.cpp \
						  src/http/api_setup_reset.cpp \
						  src/http/api_static.cpp \
						  src/http/responses.cpp \
						  src/service/dashboard_service.cpp \
						  src/service/health_service.cpp \
						  src/service/host_service.cpp \
						  src/service/settings_service.cpp \
						  src/db/note_repository.cpp \
						  src/service/note_service.cpp \
						  src/service/profile_service.cpp \
						  src/service/scheduler_service.cpp \
						  src/service/scan_service.cpp \
						  src/service/scan_service_runtime.cpp \
						  src/service/scan_persistence.cpp \
						  src/service/scan_completion.cpp \
						  src/service/scan_result_builders.cpp \
						  src/service/scan_messages.cpp \
						  src/scan/nmap.cpp \
						  src/scan/nmap_command_builder.cpp \
						  src/scan/scan_chunk_planner.cpp \
						  src/scan/scan_chunk_runner.cpp \
						  src/scan/nmap_xml_parser.cpp \
						  src/scan/scan_snapshot_merger.cpp \
						  src/scan/nmap_runner_internal.cpp \
						  src/scan/nmap_runner_common.cpp \
							  $(NMAP_RUNNER_PLATFORM_SRC) \
						  src/scan/port_spec_validator.cpp \
						  src/scan/process_renderer.cpp \
						  src/scan/scan_orchestrator.cpp \
						  src/scan/target_validation.cpp \
							  src/util/path_utils.cpp \
							  src/util/logger.cpp \
							  src/util/secret_utils.cpp \
							  src/util/string_utils.cpp \
							  src/util/time_utils.cpp
SETTINGS_ROUTE_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(SETTINGS_ROUTE_TEST_SRC)))
SETTINGS_ROUTE_TEST_DEP = $(SETTINGS_ROUTE_TEST_OBJ:.o=.d)
SETTINGS_ROUTE_TEST_TARGET = $(TEST_BIN_DIR)/settings_route_test


SCAN_REPOSITORY_TEST_SRC = test/src/db/scan_repository_test.cpp \
						   src/service/scan_persistence.cpp \
						   src/db/database.cpp \
						   src/db/schema.cpp \
						   $(SCAN_REPOSITORY_SRC) \
						   src/scan/nmap_xml_parser.cpp \
						   src/scan/port_spec_validator.cpp \
						   src/scan/target_validation.cpp \
						   src/util/file_permissions.cpp \
						   src/util/logger.cpp \
						   src/util/path_utils.cpp \
						   src/util/string_utils.cpp
SCAN_REPOSITORY_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(SCAN_REPOSITORY_TEST_SRC)))
SCAN_REPOSITORY_TEST_DEP = $(SCAN_REPOSITORY_TEST_OBJ:.o=.d)
SCAN_REPOSITORY_TEST_TARGET = $(TEST_BIN_DIR)/scan_repository_test

PRESENCE_REPOSITORY_TEST_SRC = test/src/db/presence_repository_test.cpp \
							   src/db/database.cpp \
							   src/db/schema.cpp \
							   src/db/presence_repository.cpp \
							   src/scan/target_validation.cpp \
							   src/util/file_permissions.cpp \
							   src/util/logger.cpp \
							   src/util/path_utils.cpp \
							   src/util/string_utils.cpp
PRESENCE_REPOSITORY_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(PRESENCE_REPOSITORY_TEST_SRC)))
PRESENCE_REPOSITORY_TEST_DEP = $(PRESENCE_REPOSITORY_TEST_OBJ:.o=.d)
PRESENCE_REPOSITORY_TEST_TARGET = $(TEST_BIN_DIR)/presence_repository_test

HOST_META_REPOSITORY_TEST_SRC = test/src/db/host_meta_repository_test.cpp \
								 src/db/database.cpp \
								 src/db/schema.cpp \
								 src/db/host_meta_repository.cpp \
								 src/scan/target_validation.cpp \
								 src/util/file_permissions.cpp \
								 src/util/logger.cpp \
								 src/util/path_utils.cpp \
								 src/util/string_utils.cpp
HOST_META_REPOSITORY_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(HOST_META_REPOSITORY_TEST_SRC)))
HOST_META_REPOSITORY_TEST_DEP = $(HOST_META_REPOSITORY_TEST_OBJ:.o=.d)
HOST_META_REPOSITORY_TEST_TARGET = $(TEST_BIN_DIR)/host_meta_repository_test

ALLOWED_TARGET_REPOSITORY_TEST_SRC = test/src/db/allowed_target_repository_test.cpp \
									 src/db/database.cpp \
									 src/db/schema.cpp \
									 src/db/allowed_target_repository.cpp \
									 src/util/file_permissions.cpp \
									 src/util/path_utils.cpp \
									 src/util/logger.cpp
ALLOWED_TARGET_REPOSITORY_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(ALLOWED_TARGET_REPOSITORY_TEST_SRC)))
ALLOWED_TARGET_REPOSITORY_TEST_DEP = $(ALLOWED_TARGET_REPOSITORY_TEST_OBJ:.o=.d)
ALLOWED_TARGET_REPOSITORY_TEST_TARGET = $(TEST_BIN_DIR)/allowed_target_repository_test

PRESENCE_SERVICE_TEST_SRC = test/src/service/presence_service_test.cpp \
							src/service/presence_service.cpp \
							src/service/presence_check_runner.cpp \
							src/service/presence_scheduler_service.cpp \
							src/db/database.cpp \
							src/db/schema.cpp \
							src/db/write_transaction.cpp \
							src/db/presence_repository.cpp \
							$(SCAN_REPOSITORY_SRC) \
							src/scan/target_validation.cpp \
							src/util/file_permissions.cpp \
							src/util/logger.cpp \
							src/util/path_utils.cpp \
							src/util/string_utils.cpp \
							src/util/time_utils.cpp
PRESENCE_SERVICE_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(PRESENCE_SERVICE_TEST_SRC)))
PRESENCE_SERVICE_TEST_DEP = $(PRESENCE_SERVICE_TEST_OBJ:.o=.d)
PRESENCE_SERVICE_TEST_TARGET = $(TEST_BIN_DIR)/presence_service_test

XML_PARSER_TEST_SRC = test/src/scan/nmap_xml_parser_test.cpp \
					  src/scan/nmap_xml_parser.cpp
XML_PARSER_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(XML_PARSER_TEST_SRC)))
XML_PARSER_TEST_DEP = $(XML_PARSER_TEST_OBJ:.o=.d)
XML_PARSER_TEST_TARGET = $(TEST_BIN_DIR)/nmap_xml_parser_test

LOGGER_TEST_SRC = test/src/util/logger_test.cpp \
			  src/util/logger.cpp
LOGGER_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(LOGGER_TEST_SRC)))
LOGGER_TEST_DEP = $(LOGGER_TEST_OBJ:.o=.d)
LOGGER_TEST_TARGET = $(TEST_BIN_DIR)/logger_test

LOGGER_INTEGRATION_TEST_SRC = test/src/util/logger_integration_test.cpp \
			  src/app/config.cpp \
			  src/app/logging_setup.cpp \
			  src/app/paths.cpp \
			  src/util/file_permissions.cpp \
			  src/db/database.cpp \
			  src/http/api_health.cpp \
			  src/http/responses.cpp \
			  src/scan/nmap.cpp \
			  src/scan/nmap_command_builder.cpp \
			  src/scan/nmap_xml_parser.cpp \
			  src/scan/nmap_runner_internal.cpp \
			  src/scan/nmap_runner_common.cpp \
							  $(NMAP_RUNNER_PLATFORM_SRC) \
			  src/scan/port_spec_validator.cpp \
			  src/scan/process_renderer.cpp \
			  src/scan/scan_orchestrator.cpp \
			  src/scan/target_validation.cpp \
			  src/util/path_utils.cpp \
			  src/util/logger.cpp \
			  src/util/string_utils.cpp
LOGGER_INTEGRATION_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(LOGGER_INTEGRATION_TEST_SRC)))
LOGGER_INTEGRATION_TEST_DEP = $(LOGGER_INTEGRATION_TEST_OBJ:.o=.d)
LOGGER_INTEGRATION_TEST_TARGET = $(TEST_BIN_DIR)/logger_integration_test

STARTUP_VALIDATION_TEST_SRC = test/src/app/startup_validation_test.cpp \
			  src/app/startup_validation.cpp \
			  src/app/config.cpp \
			  src/app/paths.cpp \
			  src/util/file_permissions.cpp \
			  src/scan/nmap.cpp \
			  src/scan/target_validation.cpp \
			  src/util/path_utils.cpp \
			  src/util/logger.cpp \
			  src/util/string_utils.cpp
STARTUP_VALIDATION_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(STARTUP_VALIDATION_TEST_SRC)))
STARTUP_VALIDATION_TEST_DEP = $(STARTUP_VALIDATION_TEST_OBJ:.o=.d)
STARTUP_VALIDATION_TEST_TARGET = $(TEST_BIN_DIR)/startup_validation_test

CONFIG_TEST_SRC = test/src/app/config_test.cpp \
				  src/app/config.cpp \
				  src/app/paths.cpp \
				  src/util/file_permissions.cpp \
				  src/scan/target_validation.cpp \
				  src/util/path_utils.cpp \
				  src/util/logger.cpp \
				  src/util/string_utils.cpp
CONFIG_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(CONFIG_TEST_SRC)))
CONFIG_TEST_DEP = $(CONFIG_TEST_OBJ:.o=.d)
CONFIG_TEST_TARGET = $(TEST_BIN_DIR)/config_test

TARGET_VALIDATION_TEST_SRC = test/src/scan/target_validation_test.cpp \
							  src/scan/target_validation.cpp \
							  src/util/string_utils.cpp
TARGET_VALIDATION_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(TARGET_VALIDATION_TEST_SRC)))
TARGET_VALIDATION_TEST_DEP = $(TARGET_VALIDATION_TEST_OBJ:.o=.d)
TARGET_VALIDATION_TEST_TARGET = $(TEST_BIN_DIR)/target_validation_test

PRESENCE_CHECK_RUNNER_TEST_SRC = test/src/service/presence_check_runner_test.cpp \
								 src/service/presence_check_runner.cpp \
								 src/scan/target_validation.cpp \
								 src/util/logger.cpp \
								 src/util/string_utils.cpp
PRESENCE_CHECK_RUNNER_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(PRESENCE_CHECK_RUNNER_TEST_SRC)))
PRESENCE_CHECK_RUNNER_TEST_DEP = $(PRESENCE_CHECK_RUNNER_TEST_OBJ:.o=.d)
PRESENCE_CHECK_RUNNER_TEST_TARGET = $(TEST_BIN_DIR)/presence_check_runner_test

API_GUARD_TEST_SRC = test/src/http/api_guard_test.cpp \
					 src/app/auth_config.cpp \
					 src/app/config.cpp \
					 src/app/paths.cpp \
					 src/util/file_permissions.cpp \
					 src/http/responses.cpp \
					 src/http/route_utils.cpp \
					 src/scan/target_validation.cpp \
					 src/util/logger.cpp \
					 src/util/path_utils.cpp \
					 src/util/secret_utils.cpp \
					 src/util/string_utils.cpp
API_GUARD_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(API_GUARD_TEST_SRC)))
API_GUARD_TEST_DEP = $(API_GUARD_TEST_OBJ:.o=.d)
API_GUARD_TEST_TARGET = $(TEST_BIN_DIR)/api_guard_test

SECRET_UTILS_TEST_SRC = test/src/util/secret_utils_test.cpp \
						src/util/secret_utils.cpp
SECRET_UTILS_TEST_OBJ = $(patsubst test/src/%.cpp,build/test/src/%.o,$(patsubst src/%.cpp,build/%.o,$(SECRET_UTILS_TEST_SRC)))
SECRET_UTILS_TEST_DEP = $(SECRET_UTILS_TEST_OBJ:.o=.d)
SECRET_UTILS_TEST_TARGET = $(TEST_BIN_DIR)/secret_utils_test

FAST_TEST_TARGETS = $(TEST_TARGET) $(RENDERER_TEST_TARGET) \
					$(XML_PARSER_TEST_TARGET) $(LOGGER_TEST_TARGET) \
					$(STARTUP_VALIDATION_TEST_TARGET) $(NMAP_CHECK_TEST_TARGET) \
					$(SCAN_CHUNK_RUNNER_TEST_TARGET) \
					$(TARGET_VALIDATION_TEST_TARGET) \
					$(PRESENCE_CHECK_RUNNER_TEST_TARGET) \
					$(API_GUARD_TEST_TARGET) \
					$(SECRET_UTILS_TEST_TARGET) \
					$(CONFIG_TEST_TARGET)

INTEGRATION_TEST_TARGETS = $(RUNNER_TEST_TARGET) \
						   $(SCAN_DIFF_MODEL_TEST_TARGET) \
						   $(SCAN_ABORT_SERVICE_TEST_TARGET) $(SCAN_COMPLETION_TEST_TARGET) \
						   $(HOST_SERVICE_DETAIL_TEST_TARGET) \
						   $(SETTINGS_SERVICE_TEST_TARGET) \
						   $(TOPOLOGY_SERVICE_TEST_TARGET) \
						   $(SCHEDULER_SERVICE_TEST_TARGET) \
						   $(SCAN_DETAIL_ROUTE_TEST_TARGET) $(SCAN_START_ROUTE_TEST_TARGET) \
						   $(SCAN_ABORT_ROUTE_TEST_TARGET) $(SETTINGS_ROUTE_TEST_TARGET) \
						   $(SCAN_REPOSITORY_TEST_TARGET) $(PRESENCE_REPOSITORY_TEST_TARGET) \
						   $(HOST_META_REPOSITORY_TEST_TARGET) \
						   $(ALLOWED_TARGET_REPOSITORY_TEST_TARGET) \
						   $(PRESENCE_SERVICE_TEST_TARGET) $(LOGGER_INTEGRATION_TEST_TARGET)

TEST_TARGETS = $(FAST_TEST_TARGETS) $(INTEGRATION_TEST_TARGETS)

FAST_TEST_DEPS = $(TEST_DEP) $(RENDERER_TEST_DEP) \
				 $(XML_PARSER_TEST_DEP) $(LOGGER_TEST_DEP) \
				 $(STARTUP_VALIDATION_TEST_DEP) $(NMAP_CHECK_TEST_DEP) \
				 $(SCAN_CHUNK_RUNNER_TEST_DEP) \
				 $(TARGET_VALIDATION_TEST_DEP) \
				 $(PRESENCE_CHECK_RUNNER_TEST_DEP) \
				 $(API_GUARD_TEST_DEP) \
				 $(SECRET_UTILS_TEST_DEP) \
				 $(CONFIG_TEST_DEP)

INTEGRATION_TEST_DEPS = $(RUNNER_TEST_DEP) \
						$(SCAN_DIFF_MODEL_TEST_DEP) \
						$(SCAN_ABORT_SERVICE_TEST_DEP) $(SCAN_COMPLETION_TEST_DEP) \
						$(HOST_SERVICE_DETAIL_TEST_DEP) \
						$(SETTINGS_SERVICE_TEST_DEP) \
						$(TOPOLOGY_SERVICE_TEST_DEP) \
						$(SCHEDULER_SERVICE_TEST_DEP) \
						$(SCAN_DETAIL_ROUTE_TEST_DEP) $(SCAN_START_ROUTE_TEST_DEP) \
						$(SCAN_ABORT_ROUTE_TEST_DEP) $(SETTINGS_ROUTE_TEST_DEP) \
						$(SCAN_REPOSITORY_TEST_DEP) $(PRESENCE_REPOSITORY_TEST_DEP) \
						$(HOST_META_REPOSITORY_TEST_DEP) \
						$(ALLOWED_TARGET_REPOSITORY_TEST_DEP) \
						$(PRESENCE_SERVICE_TEST_DEP) $(LOGGER_INTEGRATION_TEST_DEP) \
						$(TEST_SUPPORT_DEP)

TEST_DEPS = $(FAST_TEST_DEPS) $(INTEGRATION_TEST_DEPS)

# Explicit object dependencies
$(TEST_TARGET): $(TEST_OBJ)
$(RENDERER_TEST_TARGET): $(RENDERER_TEST_OBJ)
$(RUNNER_TEST_TARGET): $(RUNNER_TEST_OBJ)
$(NMAP_CHECK_TEST_TARGET): $(NMAP_CHECK_TEST_OBJ)
$(SCAN_CHUNK_RUNNER_TEST_TARGET): $(SCAN_CHUNK_RUNNER_TEST_OBJ)
$(XML_PARSER_TEST_TARGET): $(XML_PARSER_TEST_OBJ) $(TINYXML2_OBJ)
$(SCAN_DIFF_MODEL_TEST_TARGET): $(SCAN_DIFF_MODEL_TEST_OBJ) $(TEST_SUPPORT_OBJ) $(SQLITE_OBJ) $(TINYXML2_OBJ)
$(SCAN_ABORT_SERVICE_TEST_TARGET): $(SCAN_ABORT_SERVICE_TEST_OBJ) $(TEST_SUPPORT_OBJ) $(SQLITE_OBJ) $(TINYXML2_OBJ)
$(SCAN_COMPLETION_TEST_TARGET): $(SCAN_COMPLETION_TEST_OBJ) $(TEST_SUPPORT_OBJ) $(SQLITE_OBJ) $(TINYXML2_OBJ)
$(HOST_SERVICE_DETAIL_TEST_TARGET): $(HOST_SERVICE_DETAIL_TEST_OBJ) $(TEST_SUPPORT_OBJ) $(SQLITE_OBJ)
$(SETTINGS_SERVICE_TEST_TARGET): $(SETTINGS_SERVICE_TEST_OBJ) $(SQLITE_OBJ)
$(TOPOLOGY_SERVICE_TEST_TARGET): $(TOPOLOGY_SERVICE_TEST_OBJ) $(TEST_SUPPORT_OBJ) $(SQLITE_OBJ) $(TINYXML2_OBJ)
$(SCHEDULER_SERVICE_TEST_TARGET): $(SCHEDULER_SERVICE_TEST_OBJ) $(SQLITE_OBJ)
$(SCAN_DETAIL_ROUTE_TEST_TARGET): $(SCAN_DETAIL_ROUTE_TEST_OBJ) $(TEST_SUPPORT_OBJ) build/app/setup_bootstrap.o build/app/db_bootstrap.o build/app/logging_setup.o build/app/startup_validation.o build/http/api_presence.o build/http/api_topology.o build/service/presence_service.o build/service/presence_check_runner.o build/service/topology_service.o build/db/presence_repository.o build/db/scan_repository.o $(SQLITE_OBJ) $(TINYXML2_OBJ)
$(SCAN_START_ROUTE_TEST_TARGET): $(SCAN_START_ROUTE_TEST_OBJ) $(TEST_SUPPORT_OBJ) build/app/setup_bootstrap.o build/app/db_bootstrap.o build/app/logging_setup.o build/app/startup_validation.o build/http/api_presence.o build/http/api_topology.o build/service/presence_service.o build/service/presence_check_runner.o build/service/topology_service.o build/db/presence_repository.o build/db/scan_repository.o build/service/scan_diff_service.o build/service/scan_history_service.o $(SQLITE_OBJ) $(TINYXML2_OBJ)
$(SCAN_ABORT_ROUTE_TEST_TARGET): $(SCAN_ABORT_ROUTE_TEST_OBJ) $(TEST_SUPPORT_OBJ) build/app/setup_bootstrap.o build/app/db_bootstrap.o build/app/logging_setup.o build/app/startup_validation.o build/http/api_presence.o build/http/api_topology.o build/service/presence_service.o build/service/presence_check_runner.o build/service/topology_service.o build/db/presence_repository.o build/db/scan_repository.o build/service/scan_diff_service.o build/service/scan_history_service.o $(SQLITE_OBJ) $(TINYXML2_OBJ)
$(SETTINGS_ROUTE_TEST_TARGET): $(SETTINGS_ROUTE_TEST_OBJ) $(TEST_SUPPORT_OBJ) build/app/setup_bootstrap.o build/app/db_bootstrap.o build/app/logging_setup.o build/app/startup_validation.o build/http/api_presence.o build/http/api_topology.o build/service/presence_service.o build/service/presence_check_runner.o build/service/topology_service.o build/db/presence_repository.o build/db/scan_repository.o build/service/scan_diff_service.o build/service/scan_history_service.o $(SQLITE_OBJ) $(TINYXML2_OBJ)
$(SCAN_REPOSITORY_TEST_TARGET): $(SCAN_REPOSITORY_TEST_OBJ) $(TEST_SUPPORT_OBJ) $(SQLITE_OBJ) $(TINYXML2_OBJ)
$(PRESENCE_REPOSITORY_TEST_TARGET): $(PRESENCE_REPOSITORY_TEST_OBJ) $(SQLITE_OBJ)
$(HOST_META_REPOSITORY_TEST_TARGET): $(HOST_META_REPOSITORY_TEST_OBJ) $(SQLITE_OBJ)
$(ALLOWED_TARGET_REPOSITORY_TEST_TARGET): $(ALLOWED_TARGET_REPOSITORY_TEST_OBJ) $(SQLITE_OBJ)
$(PRESENCE_SERVICE_TEST_TARGET): $(PRESENCE_SERVICE_TEST_OBJ) $(SQLITE_OBJ)
$(TARGET_VALIDATION_TEST_TARGET): $(TARGET_VALIDATION_TEST_OBJ)
$(PRESENCE_CHECK_RUNNER_TEST_TARGET): $(PRESENCE_CHECK_RUNNER_TEST_OBJ)
$(API_GUARD_TEST_TARGET): $(API_GUARD_TEST_OBJ)
$(SECRET_UTILS_TEST_TARGET): $(SECRET_UTILS_TEST_OBJ)
$(LOGGER_TEST_TARGET): $(LOGGER_TEST_OBJ)
$(LOGGER_INTEGRATION_TEST_TARGET): $(LOGGER_INTEGRATION_TEST_OBJ) $(SQLITE_OBJ) $(TINYXML2_OBJ)
$(STARTUP_VALIDATION_TEST_TARGET): $(STARTUP_VALIDATION_TEST_OBJ)
$(CONFIG_TEST_TARGET): $(CONFIG_TEST_OBJ)

$(TEST_BIN_DIR)/%_test:
	mkdir -p $(dir $@)
	$(CXX) $(TEST_CXXFLAGS) $^ -o $@ $(LDFLAGS) $(LDLIBS)

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
