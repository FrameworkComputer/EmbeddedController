
/* Memory mapping */
#define CONFIG_FLASH_BASE       0x00000000
#define CONFIG_FLASH_SIZE       0x00040000
#define CONFIG_FLASH_BANK_SIZE  0x2000
#define CONFIG_RAM_BASE         0x20000000
#define CONFIG_RAM_SIZE         0x00008000

/* Size of one firmware image in flash */
#define CONFIG_FW_IMAGE_SIZE    (32 * 1024)
#define CONFIG_FW_RO_OFF        0
#define CONFIG_FW_A_OFF         CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_B_OFF         (2 * CONFIG_FW_IMAGE_SIZE)

/* System stack size */
#define CONFIG_STACK_SIZE 4096

/* build with assertions and debug messages */
#define CONFIG_DEBUG
