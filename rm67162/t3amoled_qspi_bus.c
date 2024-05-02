#include "t3amoled_qspi_bus.h"

#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "soc/soc_caps.h"
#include "driver/gpio.h"

#include "mphalport.h"
#include "machine_hw_spi.c"
#include "py/obj.h"
#include "py/runtime.h"
#include "py/gc.h"

#define DEBUG_printf(...)

#include <string.h>

#if MICROPY_VERSION >= MICROPY_MAKE_VERSION(1, 23, 0) // STATIC should be replaced with static.
#undef STATIC   // This may become irrelevant later on.
#define STATIC static
#endif

/*
Actual functions for qspi transmission.
*/

void hal_lcd_qspi_panel_construct(mp_obj_base_t *self)
{
    rm67162_qspi_bus_obj_t *qspi_panel_obj = (rm67162_qspi_bus_obj_t *)self;
    machine_hw_spi_obj_t *spi_obj = ((machine_hw_spi_obj_t *)qspi_panel_obj->spi_obj);
    machine_hw_spi_obj_t old_spi_obj = *spi_obj;
    if (spi_obj->state == MACHINE_HW_SPI_STATE_INIT) {
        spi_obj->state = MACHINE_HW_SPI_STATE_DEINIT;
        machine_hw_spi_deinit_internal(&old_spi_obj);
    }

    mp_hal_pin_output(qspi_panel_obj->cs_pin);
    mp_hal_pin_od_high(qspi_panel_obj->cs_pin);

    spi_bus_config_t buscfg = {
        .data0_io_num = qspi_panel_obj->databus_pins[0],
        .data1_io_num = qspi_panel_obj->databus_pins[1],
        .sclk_io_num = spi_obj->sck,
        .data2_io_num = qspi_panel_obj->databus_pins[2],
        .data3_io_num = qspi_panel_obj->databus_pins[3],
        //.max_transfer_sz = qspi_panel_obj->width * qspi_panel_obj->height * sizeof(uint16_t),
        .max_transfer_sz = (0x4000 * 16) + 8,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
    };
    esp_err_t ret = spi_bus_initialize(spi_obj->host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != 0) {
        mp_raise_msg_varg(&mp_type_OSError, "%d(spi_bus_initialize)", ret);
    }
    spi_obj->state = MACHINE_HW_SPI_STATE_INIT;

    spi_device_interface_config_t devcfg = {
        .command_bits = qspi_panel_obj->cmd_bits,
        .address_bits = 24,
        .mode = spi_obj->phase | (spi_obj->polarity << 1),
        .clock_speed_hz = qspi_panel_obj->pclk,
        .spics_io_num = -1,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .queue_size = 10,
    };

    ret = spi_bus_add_device(spi_obj->host, &devcfg, &spi_obj->spi);
    if (ret != 0) {
        mp_raise_msg_varg(&mp_type_OSError, "%d(spi_bus_add_device)", ret);
    }
}


STATIC void hal_lcd_qspi_panel_tx_param(mp_obj_base_t *self,
                                        int            lcd_cmd,
                                        const void    *param,
                                        size_t         param_size)
{
    DEBUG_printf("hal_lcd_qspi_panel_tx_param cmd: %x, param_size: %u\n", lcd_cmd, param_size);

    rm67162_qspi_bus_obj_t *qspi_panel_obj = (rm67162_qspi_bus_obj_t *)self;
    machine_hw_spi_obj_t *spi_obj = ((machine_hw_spi_obj_t *)qspi_panel_obj->spi_obj);
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.flags = (SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR);
    t.cmd = 0x02;
    t.addr = lcd_cmd << 8;
    if (param_size != 0) {
        t.tx_buffer = param;
        t.length = qspi_panel_obj->cmd_bits * param_size;
    } else {
        t.tx_buffer = NULL;
        t.length = 0;
    }
    mp_hal_pin_od_low(qspi_panel_obj->cs_pin);
    spi_device_polling_transmit(spi_obj->spi, &t);
    mp_hal_pin_od_high(qspi_panel_obj->cs_pin);
}


STATIC void hal_lcd_qspi_panel_tx_color(mp_obj_base_t *self,
                                        int            lcd_cmd,
                                        const void    *color,
                                        size_t         color_size)
{
    DEBUG_printf("hal_lcd_qspi_panel_tx_color cmd:, color_size: %u\n", /* lcd_cmd, */ color_size);

    rm67162_qspi_bus_obj_t *qspi_panel_obj = (rm67162_qspi_bus_obj_t *)self;
    machine_hw_spi_obj_t *spi_obj = ((machine_hw_spi_obj_t *)qspi_panel_obj->spi_obj);
    spi_transaction_ext_t t;

    mp_hal_pin_od_low(qspi_panel_obj->cs_pin);
    memset(&t, 0, sizeof(t));
    t.base.flags = SPI_TRANS_MODE_QIO;
    t.base.cmd = 0x32;
    t.base.addr = 0x002C00;
    spi_device_polling_transmit(spi_obj->spi, (spi_transaction_t *)&t);

    uint8_t *p_color = (uint8_t *)color;
    size_t chunk_size;
    size_t len = color_size;
    memset(&t, 0, sizeof(t));
    t.base.flags = SPI_TRANS_MODE_QIO | \
                    SPI_TRANS_VARIABLE_CMD | \
                    SPI_TRANS_VARIABLE_ADDR | \
                    SPI_TRANS_VARIABLE_DUMMY;
    t.command_bits = 0;
    t.address_bits = 0;
    t.dummy_bits = 0;
    
    do {
        if (len > 0x8000) { //32 KB
            chunk_size = 0x8000;
        } else {
            chunk_size = len;
        }
        t.base.tx_buffer = p_color;
        t.base.length = chunk_size * 8;
        spi_device_polling_transmit(spi_obj->spi, (spi_transaction_t *)&t);
        len -= chunk_size;
        p_color += chunk_size;
    } while (len > 0);

    mp_hal_pin_od_high(qspi_panel_obj->cs_pin);
}


STATIC void hal_lcd_qspi_panel_deinit(mp_obj_base_t *self)
{
    rm67162_qspi_bus_obj_t *qspi_panel_obj = (rm67162_qspi_bus_obj_t *)self;
    machine_hw_spi_obj_t *spi_obj = ((machine_hw_spi_obj_t *)qspi_panel_obj->spi_obj);
    
    if (spi_obj->state == MACHINE_HW_SPI_STATE_INIT) {
        spi_obj->state = MACHINE_HW_SPI_STATE_DEINIT;
        machine_hw_spi_deinit_internal(spi_obj);
    }
}


STATIC void rm67162_qspi_bus_print(const mp_print_t *print,
                                    mp_obj_t          self_in,
                                    mp_print_kind_t   kind)
{
    (void) kind;
    rm67162_qspi_bus_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(
        print,
        "<QSPI Panel SPI=%p, dc=%p, cs=%p, width=%u, height=%u, cmd_bits=%u, param_bits=%u>",
        self->spi_obj,
        self->dc,
        self->cs,
        self->width,
        self->height,
        self->cmd_bits,
        self->param_bits
    );
}


STATIC mp_obj_t rm67162_qspi_bus_make_new(const mp_obj_type_t *type,
                                           size_t               n_args,
                                           size_t               n_kw,
                                           const mp_obj_t      *all_args)
{
    enum {
        ARG_spi,
        ARG_data,
        ARG_dc,
        ARG_cs,
        ARG_pclk,
        ARG_width,
        ARG_height,
        ARG_cmd_bits,
        ARG_param_bits
    };
    const mp_arg_t make_new_args[] = {
        { MP_QSTR_spi,              MP_ARG_OBJ | MP_ARG_KW_ONLY | MP_ARG_REQUIRED        },
        { MP_QSTR_data,             MP_ARG_OBJ | MP_ARG_KW_ONLY | MP_ARG_REQUIRED        },
        { MP_QSTR_dc,               MP_ARG_OBJ | MP_ARG_KW_ONLY | MP_ARG_REQUIRED        },
        { MP_QSTR_cs,               MP_ARG_OBJ | MP_ARG_KW_ONLY,  {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_pclk,             MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = 10000000   } },
        { MP_QSTR_width,            MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = 240        } },
        { MP_QSTR_height,           MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = 240        } },
        { MP_QSTR_cmd_bits,         MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = 8         }  },
        { MP_QSTR_param_bits,       MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = 8         }  },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(make_new_args)];
    mp_arg_parse_all_kw_array(
        n_args,
        n_kw,
        all_args,
        MP_ARRAY_SIZE(make_new_args),
        make_new_args, args
    );

    // create new object
    rm67162_qspi_bus_obj_t *self = m_new_obj(rm67162_qspi_bus_obj_t);
    self->base.type = &rm67162_qspi_bus_type;
    self->spi_obj = MP_OBJ_TO_PTR(args[ARG_spi].u_obj);

    // data bus
    mp_obj_tuple_t *t = MP_OBJ_TO_PTR(args[ARG_data].u_obj);
    for (size_t i = 0; i < t->len; i++) {
        if (i < 4) {
            self->databus_pins[i] = mp_hal_get_pin_obj(t->items[i]);
        }
    }
    self->dc_pin     = mp_hal_get_pin_obj(args[ARG_dc].u_obj);
    self->cs_pin     = mp_hal_get_pin_obj(args[ARG_cs].u_obj);
    self->pclk       = args[ARG_pclk].u_int;
    self->width      = args[ARG_width].u_int;
    self->height     = args[ARG_height].u_int;
    self->cmd_bits   = args[ARG_cmd_bits].u_int;
    self->param_bits = args[ARG_param_bits].u_int;

    hal_lcd_qspi_panel_construct(&self->base);
    return MP_OBJ_FROM_PTR(self);
}


STATIC mp_obj_t rm67162_qspi_bus_tx_param(size_t n_args, const mp_obj_t *args_in)
{
    mp_obj_base_t *self = (mp_obj_base_t *)MP_OBJ_TO_PTR(args_in[0]);
    int cmd = mp_obj_get_int(args_in[1]);
    if (n_args == 3) { //if only 3 args are passed, then the len variable is not passed, hence this is a cmd, else it is a data
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(args_in[2], &bufinfo, MP_BUFFER_READ);
        hal_lcd_qspi_panel_tx_param(self, cmd, bufinfo.buf, bufinfo.len);
    } else {
        hal_lcd_qspi_panel_tx_param(self, cmd, NULL, 0);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_qspi_bus_tx_param_obj, 2, 3, rm67162_qspi_bus_tx_param);


STATIC mp_obj_t rm67162_qspi_bus_tx_color(size_t n_args, const mp_obj_t *args_in)
{
    mp_obj_base_t *self = (mp_obj_base_t *)MP_OBJ_TO_PTR(args_in[0]);
    int cmd = mp_obj_get_int(args_in[1]);

    if (n_args == 3) {
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(args_in[2], &bufinfo, MP_BUFFER_READ);
        hal_lcd_qspi_panel_tx_color(self, cmd, bufinfo.buf, bufinfo.len);
    } else {
        hal_lcd_qspi_panel_tx_color(self, cmd, NULL, 0);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_qspi_bus_tx_color_obj, 2, 3, rm67162_qspi_bus_tx_color);


STATIC mp_obj_t rm67162_qspi_bus_deinit(mp_obj_t self_in)
{
    mp_obj_base_t *self = (mp_obj_base_t *)MP_OBJ_TO_PTR(self_in);

    hal_lcd_qspi_panel_deinit(self);
    // m_del_obj(mp_lcd_spi_panel_obj_t, self);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(rm67162_qspi_bus_deinit_obj, rm67162_qspi_bus_deinit);


STATIC const mp_rom_map_elem_t rm67162_qspi_bus_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_tx_param), MP_ROM_PTR(&rm67162_qspi_bus_tx_param_obj) },
    { MP_ROM_QSTR(MP_QSTR_tx_color), MP_ROM_PTR(&rm67162_qspi_bus_tx_color_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit),   MP_ROM_PTR(&rm67162_qspi_bus_deinit_obj)   },
    { MP_ROM_QSTR(MP_QSTR___del__),  MP_ROM_PTR(&rm67162_qspi_bus_deinit_obj)   },
};
STATIC MP_DEFINE_CONST_DICT(rm67162_qspi_bus_locals_dict, rm67162_qspi_bus_locals_dict_table);


STATIC const rm67162_panel_p_t mp_lcd_panel_p = {
    .tx_param = hal_lcd_qspi_panel_tx_param,
    .tx_color = hal_lcd_qspi_panel_tx_color,
    .deinit = hal_lcd_qspi_panel_deinit
};


#ifdef MP_OBJ_TYPE_GET_SLOT
MP_DEFINE_CONST_OBJ_TYPE(
    rm67162_qspi_bus_type,
    MP_QSTR_QSPI_Panel,
    MP_TYPE_FLAG_NONE,
    print, rm67162_qspi_bus_print,
    make_new, rm67162_qspi_bus_make_new,
    protocol, &mp_lcd_panel_p,
    locals_dict, (mp_obj_dict_t *)&rm67162_qspi_bus_locals_dict
);
#else
const mp_obj_type_t rm67162_qspi_bus_type = {
    { &mp_type_type },
    .name = MP_QSTR_QSPI_Panel,
    .print = rm67162_qspi_bus_print,
    .make_new = rm67162_qspi_bus_make_new,
    .protocol = &mp_lcd_panel_p,
    .locals_dict = (mp_obj_dict_t *)&rm67162_qspi_bus_locals_dict,
};
#endif
