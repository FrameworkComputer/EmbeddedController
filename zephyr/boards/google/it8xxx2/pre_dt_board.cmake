# Suppress "unique_unit_address_if_enabled" warnings, expected for nodes:
# - /soc/gpio@f01d00
# - /soc/cros-kb-raw@f01d00
# - /soc/kbd@f01d00
list(APPEND EXTRA_DTC_FLAGS "-Wno-unique_unit_address_if_enabled")
