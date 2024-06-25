# Suppress "unique_unit_address_if_enabled" warnings, expected for nodes:
# - /soc/cros-kb-raw@400a3000
# - /soc/kbd@400a3000
list(APPEND EXTRA_DTC_FLAGS "-Wno-unique_unit_address_if_enabled")
