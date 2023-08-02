#include "rm67162.h"
#include "t3amoled_qspi_bus.h"

#include "py/obj.h"

STATIC const mp_map_elem_t mp_module_rm67162_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),   MP_OBJ_NEW_QSTR(MP_QSTR_rm67162)          },
    { MP_ROM_QSTR(MP_QSTR_RM67162),    (mp_obj_t)&rm67162_RM67162_type       },
    { MP_ROM_QSTR(MP_QSTR_QSPIPanel),  (mp_obj_t)&rm67162_qspi_bus_type      },

    { MP_ROM_QSTR(MP_QSTR_RGB),        MP_ROM_INT(COLOR_SPACE_RGB)           },
    { MP_ROM_QSTR(MP_QSTR_BGR),        MP_ROM_INT(COLOR_SPACE_BGR)           },
    { MP_ROM_QSTR(MP_QSTR_MONOCHROME), MP_ROM_INT(COLOR_SPACE_MONOCHROME)    },
    { MP_ROM_QSTR(MP_QSTR_BLACK),      MP_ROM_INT(BLACK)                     },
    { MP_ROM_QSTR(MP_QSTR_BLUE),       MP_ROM_INT(BLUE)                      },
    { MP_ROM_QSTR(MP_QSTR_RED),        MP_ROM_INT(RED)                       },
    { MP_ROM_QSTR(MP_QSTR_GREEN),      MP_ROM_INT(GREEN)                     },
    { MP_ROM_QSTR(MP_QSTR_CYAN),       MP_ROM_INT(CYAN)                      },
    { MP_ROM_QSTR(MP_QSTR_MAGENTA),    MP_ROM_INT(MAGENTA)                   },
    { MP_ROM_QSTR(MP_QSTR_YELLOW),     MP_ROM_INT(YELLOW)                    },
    { MP_ROM_QSTR(MP_QSTR_WHITE),      MP_ROM_INT(WHITE)                     },
};
STATIC MP_DEFINE_CONST_DICT(mp_module_rm67162_globals, mp_module_rm67162_globals_table);


const mp_obj_module_t mp_module_rm67162 = {
    .base    = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&mp_module_rm67162_globals,
};


#if MICROPY_VERSION >= 0x011300 // MicroPython 1.19 or later
MP_REGISTER_MODULE(MP_QSTR_rm67162, mp_module_rm67162);
#else
MP_REGISTER_MODULE(MP_QSTR_rm67162, mp_module_rm67162, MODULE_RM67162_ENABLE);
#endif
