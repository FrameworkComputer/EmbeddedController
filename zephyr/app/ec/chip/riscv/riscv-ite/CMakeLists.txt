if (CONFIG_ESPI_IT8XXX2)
    zephyr_library_sources_ifdef(CONFIG_AP_POWER_CONTROL it8xxx2-espi.c)
endif ()
