#include "rm67162.h"
#include "t3amoled_qspi_bus.h"

#include "py/obj.h"
#include "py/runtime.h"
#include "mphalport.h"
#include "py/gc.h"
#include "py/objstr.h"

#include "esp_lcd_panel_io.h"
#include "driver/spi_master.h"

#include <string.h>

#if MICROPY_VERSION >= MICROPY_MAKE_VERSION(1, 23, 0) // STATIC should be replaced with static.
#undef STATIC   // This may become irrelevant later on.
#define STATIC static
#endif

#define _swap_int16_t(a, b) { int16_t t = a; a = b; b = t; }
#define _swap_bytes(val) ((((val) >> 8) & 0x00FF) | (((val) << 8) & 0xFF00))

#define ABS(N) (((N) < 0) ? (-(N)) : (N))
#define mp_hal_delay_ms(delay) (mp_hal_delay_us(delay * 1000))

const char* color_space_desc[] = {
    "RGB",
    "BGR",
    "MONOCHROME"
};

STATIC const rm67162_rotation_t ORIENTATIONS_GENERAL[4] = {
    { 0x00, 0, 0, 0, 0}, // { madctl, width, height, colstart, rowstart }
    { 0x60, 0, 0, 0, 0},
    { 0xC0, 0, 0, 0, 0},
    { 0xA0, 0, 0, 0, 0}
};

STATIC const rm67162_rotation_t ORIENTATIONS_240x536[4] = {
    { 0x00, 240, 536, 0, 0 },
    { 0x60, 536, 240, 0, 0 },
    { 0xC0, 240, 536, 0, 0 },
    { 0xA0, 536, 240, 0, 0 }
};

int mod(int x, int m) {
    int r = x % m;
    return (r < 0) ? r + m : r;
}


/*----------------------------------------------------------------------------------------------------
Below are transmission related functions.
-----------------------------------------------------------------------------------------------------*/


STATIC void write_color(rm67162_RM67162_obj_t *self, const void *buf, int len) {
    if (self->lcd_panel_p) {
            self->lcd_panel_p->tx_color(self->bus_obj, 0, buf, len);
    } else {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Failed to find the panel object."));
    }
}


STATIC void write_spi(rm67162_RM67162_obj_t *self, int cmd, const void *buf, int len) {
    if (self->lcd_panel_p) {
            self->lcd_panel_p->tx_param(self->bus_obj, cmd, buf, len);
    } else {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Failed to find the panel object."));
    }
}


/*----------------------------------------------------------------------------------------------------
Below are initialization related functions.
-----------------------------------------------------------------------------------------------------*/


STATIC void frame_buffer_alloc(rm67162_RM67162_obj_t *self, int len) {
    self->frame_buffer_size = len;
    //self->frame_buffer = heap_caps_malloc(self->frame_buffer_size, MALLOC_CAP_DMA);
    self->frame_buffer = gc_alloc(self->frame_buffer_size, 0);
    //self->frame_buffer = malloc(self->frame_buffer_size);
    
    if (self->frame_buffer == NULL) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Failed to allocate DMA'able framebuffer."));
    }
    memset(self->frame_buffer, 0, self->frame_buffer_size);
}


STATIC void set_rotation(rm67162_RM67162_obj_t *self, uint8_t rotation) {
    self->madctl_val &= 0x1F;
    self->madctl_val |= self->rotations[rotation].madctl;

    write_spi(self, LCD_CMD_MADCTL, (uint8_t[]) { self->madctl_val }, 1);

    self->width = self->rotations[rotation].width;
    self->max_width_value = self->width - 1;
    self->height = self->rotations[rotation].height;
    self->max_height_value = self->height - 1;
    self->x_gap = self->rotations[rotation].colstart;
    self->y_gap = self->rotations[rotation].rowstart;
}


STATIC void rm67162_RM67162_print(const mp_print_t *print,
                                 mp_obj_t          self_in,
                                 mp_print_kind_t   kind)
{
    (void) kind;
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(
        print,
        "<RM67162 bus=%p, reset=%p, color_space=%s, bpp=%u>",
        self->bus_obj,
        self->reset,
        color_space_desc[self->color_space],
        self->bpp
    );
}


mp_obj_t rm67162_RM67162_make_new(const mp_obj_type_t *type,
                                 size_t               n_args,
                                 size_t               n_kw,
                                 const mp_obj_t      *all_args)
{
    enum {
        ARG_bus,
        ARG_reset,
        ARG_reset_level,
        ARG_color_space,
        ARG_bpp
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_bus,            MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_obj = MP_OBJ_NULL}     },
        { MP_QSTR_reset,          MP_ARG_OBJ | MP_ARG_KW_ONLY,  {.u_obj = MP_OBJ_NULL}     },
        { MP_QSTR_reset_level,    MP_ARG_BOOL | MP_ARG_KW_ONLY, {.u_bool = false}          },
        { MP_QSTR_color_space,    MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = COLOR_SPACE_RGB} },
        { MP_QSTR_bpp,            MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = 16}              },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(
        n_args,
        n_kw,
        all_args,
        MP_ARRAY_SIZE(allowed_args),
        allowed_args,
        args
    );

    // create new object
    rm67162_RM67162_obj_t *self = m_new_obj(rm67162_RM67162_obj_t);
    self->base.type = &rm67162_RM67162_type;

    self->bus_obj = (mp_obj_base_t *)MP_OBJ_TO_PTR(args[ARG_bus].u_obj);
#ifdef MP_OBJ_TYPE_GET_SLOT
    self->lcd_panel_p = (rm67162_panel_p_t *)MP_OBJ_TYPE_GET_SLOT(self->bus_obj->type, protocol);
#else
    self->lcd_panel_p = (rm67162_panel_p_t *)self->bus_obj->type->protocol;
#endif

    // self->max_width_value etc will be initialized in the rotation later.
    self->width = ((rm67162_qspi_bus_obj_t *)self->bus_obj)->width;
    self->height = ((rm67162_qspi_bus_obj_t *)self->bus_obj)->height;

    // 2 bytes for each pixel. so maximum will be width * height * 2
    frame_buffer_alloc(self, self->width * self->height * 2);

    self->reset       = args[ARG_reset].u_obj;
    self->reset_level = args[ARG_reset_level].u_bool;
    self->color_space = args[ARG_color_space].u_int;
    self->bpp         = args[ARG_bpp].u_int;

    // reset
    if (self->reset != MP_OBJ_NULL) {
        mp_hal_pin_obj_t reset_pin = mp_hal_get_pin_obj(self->reset);
        mp_hal_pin_output(reset_pin);
    }

    switch (self->color_space) {
        case COLOR_SPACE_RGB:
            self->madctl_val = 0;
        break;

        case COLOR_SPACE_BGR:
            self->madctl_val |= (1 << 3);
        break;

        default:
            mp_raise_ValueError(MP_ERROR_TEXT("unsupported color space"));
        break;
    }

    switch (self->bpp) {
        case 16:
            self->colmod_cal = 0x75;
            self->fb_bpp = 16;
        break;

        case 18:
            self->colmod_cal = 0x76;
            self->fb_bpp = 24;
        break;

        case 24:
            self->colmod_cal = 0x77;
            self->fb_bpp = 24;
        break;

        default:
            mp_raise_ValueError(MP_ERROR_TEXT("unsupported pixel width"));
        break;
    }

    bzero(&self->rotations, sizeof(self->rotations));
    if ((self->width == 240 && self->height == 536) || \
        (self->width == 536 && self->height == 240)) {
        memcpy(&self->rotations, ORIENTATIONS_240x536, sizeof(ORIENTATIONS_240x536));
    } else {
        mp_warning(NULL, "rotation parameter not detected");
        mp_warning(NULL, "use default rotation parameters");
        memcpy(&self->rotations, ORIENTATIONS_GENERAL, sizeof(ORIENTATIONS_GENERAL));
        self->rotations[0].width = self->width;
        self->rotations[0].height = self->height;
        self->rotations[1].width = self->height;
        self->rotations[1].height = self->width;
        self->rotations[2].width = self->width;
        self->rotations[2].height = self->height;
        self->rotations[3].width = self->height;
        self->rotations[3].height = self->width;
    }
    set_rotation(self, 0);

    return MP_OBJ_FROM_PTR(self);
}


STATIC mp_obj_t rm67162_RM67162_deinit(mp_obj_t self_in)
{
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->lcd_panel_p) {
        self->lcd_panel_p->deinit(self->bus_obj);
    }

    gc_free(self->frame_buffer);

    //m_del_obj(rm67162_RM67162_obj_t, self); 
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(rm67162_RM67162_deinit_obj, rm67162_RM67162_deinit);


STATIC mp_obj_t rm67162_RM67162_reset(mp_obj_t self_in)
{
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->reset != MP_OBJ_NULL) {
        mp_hal_pin_obj_t reset_pin = mp_hal_get_pin_obj(self->reset);
        mp_hal_pin_write(reset_pin, self->reset_level);
        mp_hal_delay_ms(300);    
        mp_hal_pin_write(reset_pin, !self->reset_level);
        mp_hal_delay_ms(200);    
    } else {
        write_spi(self, LCD_CMD_SWRESET, NULL, 0);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(rm67162_RM67162_reset_obj, rm67162_RM67162_reset);


STATIC mp_obj_t rm67162_RM67162_init(mp_obj_t self_in)
{
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(self_in);

    write_spi(self, LCD_CMD_SLPOUT, NULL, 0);     //sleep out
    mp_hal_delay_ms(100);    

    write_spi(self, LCD_CMD_MADCTL, (uint8_t[]) {
        self->madctl_val,
    }, 1);

    write_spi(self, LCD_CMD_MADCTL, (uint8_t[]) {
        self->madctl_val,
    }, 1);

    write_spi(self, LCD_CMD_COLMOD, (uint8_t[]) {
        self->colmod_cal,
    }, 1);

    // turn on display
    write_spi(self, LCD_CMD_DISPON, NULL, 0);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(rm67162_RM67162_init_obj, rm67162_RM67162_init);


STATIC mp_obj_t rm67162_RM67162_send_cmd(size_t n_args, const mp_obj_t *args_in)
{
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint8_t cmd = mp_obj_get_int(args_in[1]);
    uint8_t c_bits = mp_obj_get_int(args_in[2]);
    uint8_t len = mp_obj_get_int(args_in[3]);

    if (len <= 0) {
        write_spi(self, cmd, NULL, 0);
    } else {
        write_spi(self, cmd, (uint8_t[]){c_bits}, len);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_send_cmd_obj, 4, 4, rm67162_RM67162_send_cmd);


/*-----------------------------------------------------------------------------------------------------
Below are drawing functions.
------------------------------------------------------------------------------------------------------*/


STATIC uint16_t colorRGB(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);
    return _swap_bytes(c);
}


STATIC mp_obj_t rm67162_RM67162_colorRGB(size_t n_args, const mp_obj_t *args_in) {
    return MP_OBJ_NEW_SMALL_INT(colorRGB(
        (uint8_t)mp_obj_get_int(args_in[1]),
        (uint8_t)mp_obj_get_int(args_in[2]),
        (uint8_t)mp_obj_get_int(args_in[3])));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_colorRGB_obj, 4, 4, rm67162_RM67162_colorRGB);


STATIC void set_area(rm67162_RM67162_obj_t *self, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    if (x0 > x1 || x1 > self->max_width_value) {
        return;
    }
    if (y0 > y1 || y1 > self->max_height_value) {
        return;
    }

    uint8_t bufx[4] = {
        ((x0 >> 8) & 0x03),
        (x0 & 0xFF),
        ((x1 >> 8) & 0x03),
        (x1 & 0xFF)};
    uint8_t bufy[4] = {
        ((y0 >> 8) & 0x03),
        (y0 & 0xFF),
        ((y1 >> 8) & 0x03),
        (y1 & 0xFF)};
    write_spi(self, LCD_CMD_CASET, bufx, 4);
    write_spi(self, LCD_CMD_RASET, bufy, 4);
    write_spi(self, LCD_CMD_RAMWR, NULL, 0);
}

// this function is extremely dangerous and should be called with a lot of care.
STATIC void fill_color_buffer(rm67162_RM67162_obj_t *self, uint32_t color, int len /*in pixel*/) {
    if (len > self->frame_buffer_size / 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("fill_color_buffer: error, maximum length exceeded, please check dimensions."));
        return;
    }
    uint32_t *buffer = (uint32_t *)self->frame_buffer;
    color = (color << 16) | color;
    // this ensures that the framebuffer is overfilled rather than unfilled.
    // also because the framebuffer_size is always even, you should not worry
    // about exceeding it.
    size_t size = (len + 1) / 2; 
    while (size--) {
        *buffer++ = color;
    }
    write_color(self, self->frame_buffer, len * 2);
}


STATIC void draw_pixel(rm67162_RM67162_obj_t *self, uint16_t x, uint16_t y, uint16_t color) {
    set_area(self, x, y, x, y);
    write_color(self, (uint8_t *) &color, 2);
}


STATIC mp_obj_t rm67162_RM67162_pixel(size_t n_args, const mp_obj_t *args_in) {
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint16_t x = mp_obj_get_int(args_in[1]);
    uint16_t y = mp_obj_get_int(args_in[2]);
    uint16_t color = mp_obj_get_int(args_in[3]);

    draw_pixel(self, x, y, color);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_pixel_obj, 4, 4, rm67162_RM67162_pixel);


// this can be replaced by fill_rect
STATIC void fast_fill(rm67162_RM67162_obj_t *self, uint16_t color) {
    set_area(self, 0, 0, self->max_width_value, self->max_height_value);
    fill_color_buffer(self, color, self->frame_buffer_size / 2);
}


STATIC mp_obj_t rm67162_RM67162_fill(size_t n_args, const mp_obj_t *args_in) {
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint16_t color = mp_obj_get_int(args_in[1]);

    fast_fill(self, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_fill_obj, 2, 2, rm67162_RM67162_fill);


STATIC void fast_hline(rm67162_RM67162_obj_t *self, int x, int y, uint16_t l, uint16_t color) {
    if (y < 0) {
        return;
    }
    if (l == 0) {
        return;
    }

    if (l == 1) {
        draw_pixel(self, x, y, color);
    } else {
        if (x < 0) {
            l += x;
            x = 0;
        }
        if (x > self->max_width_value) {
            x = self->max_width_value; // This is to prevent overflow that could occur at *1
        }
        if (x + l > self->max_width_value) {
            l = self->max_width_value - x; // *1
        } 
        set_area(self, x, y, x + l, y);
        fill_color_buffer(self, color, l + 1);
    }
}


STATIC void fast_vline(rm67162_RM67162_obj_t *self, int x, int y, uint16_t l, uint16_t color) {
    if (x < 0) {
        return;
    }
    if (l == 0) {
        return;
    }

    if (l == 1) {
        draw_pixel(self, x, y, color);
    } else {
        if (y < 0) {
            l += y;
            y = 0;
        }
        if (y > self->max_width_value) {
            y = self->max_width_value; // This is to prevent overflow that could occur at *2
        }
        if (y + l > self->max_height_value) {
            l = self->max_height_value - y; // *2
        }
        set_area(self, x, y, x, y + l);
        fill_color_buffer(self, color, l + 1);
    }
}

STATIC mp_obj_t rm67162_RM67162_hline(size_t n_args, const mp_obj_t *args_in) {
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    int x = mp_obj_get_int(args_in[1]);
    int y = mp_obj_get_int(args_in[2]);
    uint16_t l = mp_obj_get_int(args_in[3]);
    uint16_t color = mp_obj_get_int(args_in[4]);

    fast_hline(self, x, y, l, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_hline_obj, 5, 5, rm67162_RM67162_hline);


STATIC mp_obj_t rm67162_RM67162_vline(size_t n_args, const mp_obj_t *args_in) {
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    int x = mp_obj_get_int(args_in[1]);
    int y = mp_obj_get_int(args_in[2]);
    uint16_t l = mp_obj_get_int(args_in[3]);
    uint16_t color = mp_obj_get_int(args_in[4]);

    fast_vline(self, x, y, l, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_vline_obj, 5, 5, rm67162_RM67162_vline);



STATIC void rect(rm67162_RM67162_obj_t *self, uint16_t x, uint16_t y, uint16_t w, uint16_t l, uint16_t color) {
    fast_hline(self, x, y, w - 1, color);
    fast_hline(self, x, y + l - 1, w - 1, color);
    fast_vline(self, x, y, l - 1, color);
    fast_vline(self, x + w - 1, y, l - 1, color);
}


STATIC mp_obj_t rm67162_RM67162_rect(size_t n_args, const mp_obj_t *args_in) {
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint16_t x = mp_obj_get_int(args_in[1]);
    uint16_t y = mp_obj_get_int(args_in[2]);
    uint16_t w = mp_obj_get_int(args_in[3]);
    uint16_t l = mp_obj_get_int(args_in[4]);
    uint16_t color = mp_obj_get_int(args_in[5]);

    rect(self, x, y, w, l, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_rect_obj, 6, 6, rm67162_RM67162_rect);


STATIC void fill_rect(rm67162_RM67162_obj_t *self, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    set_area(self, x, y, x + w - 1, y + h - 1);
    fill_color_buffer(self, color, w * h);
}


STATIC mp_obj_t rm67162_RM67162_fill_rect(size_t n_args, const mp_obj_t *args_in) {
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint16_t x = mp_obj_get_int(args_in[1]);
    uint16_t y = mp_obj_get_int(args_in[2]);
    uint16_t w = mp_obj_get_int(args_in[3]);
    uint16_t l = mp_obj_get_int(args_in[4]);
    uint16_t color = mp_obj_get_int(args_in[5]);

    fill_rect(self, x, y, w, l, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_fill_rect_obj, 6, 6, rm67162_RM67162_fill_rect);

STATIC void fill_bubble_rect(rm67162_RM67162_obj_t *self, int xs, int ys, int w, int h, uint16_t color) {
    if (xs + w > self->width || ys + h > self->height) {
        return;
    }
    int bubble_size;
    if (w < h) {
        bubble_size = w / 4;
    } else {
        bubble_size = h / 4;
    }
    
    int xm = xs + bubble_size;
    int ym = ys + bubble_size;
    int x = 0;
    int y = bubble_size;
    int p = 1 - bubble_size;
    
    if ((w < (bubble_size * 2)) | (h < (bubble_size * 2))){
        return;
    } else {
        fill_rect(self, xs, ys + bubble_size - 1, w, h - bubble_size * 2, color);
    }

    while (x <= y) {
        // top left to right
        fast_hline(self, xm - x, ym - y, w - bubble_size * 2 + x * 2 - 1, color);
        fast_hline(self, xm - y, ym - x, w - bubble_size * 2 + y * 2 - 1, color);
        
        // bottom left to right
        fast_hline(self, xm - x, ym + h - bubble_size * 2 + y - 1, w - bubble_size * 2 + x * 2 - 1, color);
        fast_hline(self, xm - y, ym + h - bubble_size * 2 + x - 1, w - bubble_size * 2 + y * 2 - 1, color);
        
        if (p < 0) {
            p += 2 * x + 3;
        } else {
            p += 2 * (x - y) + 5;
            y -= 1;
        } 
        x += 1;
    }
}


STATIC mp_obj_t rm67162_RM67162_fill_bubble_rect(size_t n_args, const mp_obj_t *args_in) {
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    int xs = mp_obj_get_int(args_in[1]);
    int ys = mp_obj_get_int(args_in[2]);
    int w = mp_obj_get_int(args_in[3]);
    int h = mp_obj_get_int(args_in[4]);
    uint16_t color = mp_obj_get_int(args_in[5]);

    fill_bubble_rect(self, xs, ys, w, h, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_fill_bubble_rect_obj, 6, 6, rm67162_RM67162_fill_bubble_rect);

STATIC void bubble_rect(rm67162_RM67162_obj_t *self, int xs, int ys, int w, int h, uint16_t color) {
    if (xs + w > self->width || ys + h > self->height) {
        return;
    }
    int bubble_size;
    if (w < h) {
        bubble_size = w / 4;
    } else {
        bubble_size = h / 4;
    }
    
    int xm = xs + bubble_size;
    int ym = ys + bubble_size;
    int x = 0;
    int y = bubble_size;
    int p = 1 - bubble_size;
    
    if ((w < (bubble_size * 2)) | (h < (bubble_size * 2))){
        return;
    } else {
        fast_hline(self, xs + bubble_size - 1, ys, w - bubble_size * 2, color);
        fast_hline(self, xs + bubble_size - 1, ys + h - 1, w - bubble_size * 2, color);
        fast_vline(self, xs, ys + bubble_size - 1, h - bubble_size * 2, color);
        fast_vline(self, xs + w -1, ys + bubble_size - 1, h - bubble_size * 2, color);
    }

    while (x <= y){
        // top left
        draw_pixel(self, xm - x, ym - y, color);
        draw_pixel(self, xm - y, ym - x, color);
        
        // top right
        draw_pixel(self, xm + w - bubble_size * 2 + x - 1, ym - y, color);
        draw_pixel(self, xm + w - bubble_size * 2 + y - 1, ym - x, color);
        
        // bottom left
        draw_pixel(self, xm - x, ym + h - bubble_size * 2 + y - 1, color);
        draw_pixel(self, xm - y, ym + h - bubble_size * 2 + x - 1, color);
        
        // bottom right
        draw_pixel(self, xm + w - bubble_size * 2 + x - 1, ym + h - bubble_size * 2 + y - 1, color);
        draw_pixel(self, xm + w - bubble_size * 2 + y - 1, ym + h - bubble_size * 2 + x - 1, color);
        
        if (p < 0) {
            p += 2 * x + 3;
        } else {
            p += 2 * (x - y) + 5;
            y -= 1;
        }
        x += 1;
    }
}


STATIC mp_obj_t rm67162_RM67162_bubble_rect(size_t n_args, const mp_obj_t *args_in) {
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    int xs = mp_obj_get_int(args_in[1]);
    int ys = mp_obj_get_int(args_in[2]);
    int w = mp_obj_get_int(args_in[3]);
    int h = mp_obj_get_int(args_in[4]);
    uint16_t color = mp_obj_get_int(args_in[5]);

    bubble_rect(self, xs, ys, w, h, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_bubble_rect_obj, 6, 6, rm67162_RM67162_bubble_rect);


/*
Similar to: https://en.wikipedia.org/wiki/Midpoint_circle_algorithm
*/
STATIC void circle(rm67162_RM67162_obj_t *self, int xm, int ym, int r, uint16_t color) {
    int x = 0;
    int y = r;
    int p = 1 - r;

    while (x <= y) {
        draw_pixel(self, xm + x, ym + y, color);
        draw_pixel(self, xm + x, ym - y, color);
        draw_pixel(self, xm - x, ym + y, color);
        draw_pixel(self, xm - x, ym - y, color);
        draw_pixel(self, xm + y, ym + x, color);
        draw_pixel(self, xm + y, ym - x, color);
        draw_pixel(self, xm - y, ym + x, color);
        draw_pixel(self, xm - y, ym - x, color);

        if (p < 0) {
            p += 2 * x + 3;
        } else {
            p += 2 * (x - y) + 5;
            y -= 1;
        }
        x += 1;
    }
}


STATIC mp_obj_t rm67162_RM67162_circle(size_t n_args, const mp_obj_t *args_in) {
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    int xm = mp_obj_get_int(args_in[1]);
    int ym = mp_obj_get_int(args_in[2]);
    int r = mp_obj_get_int(args_in[3]);
    uint16_t color = mp_obj_get_int(args_in[4]);

    circle(self, xm, ym, r, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_circle_obj, 5, 5, rm67162_RM67162_circle);


STATIC void fill_circle(rm67162_RM67162_obj_t *self, int xm, int ym, int r, uint16_t color) {
    int x = 0;
    int y = r;
    int p = 1 - r;

    while (x <= y) {
        fast_vline(self, xm + x, ym - y, 2 * y, color);
        fast_vline(self, xm - x, ym - y, 2 * y, color);
        fast_vline(self, xm + y, ym - x, 2 * x, color);
        fast_vline(self, xm - y, ym - x, 2 * x, color);

        if (p < 0) {
            p += 2 * x + 3;
        } else {
            p += 2 * (x - y) + 5;
            y -= 1;
        }
        x += 1;
    }
}


STATIC mp_obj_t rm67162_RM67162_fill_circle(size_t n_args, const mp_obj_t *args_in) {
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    int xm = mp_obj_get_int(args_in[1]);
    int ym = mp_obj_get_int(args_in[2]);
    int r = mp_obj_get_int(args_in[3]);
    uint16_t color = mp_obj_get_int(args_in[4]);

    fill_circle(self, xm, ym, r, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_fill_circle_obj, 5, 5, rm67162_RM67162_fill_circle);


STATIC void line(rm67162_RM67162_obj_t *self, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    bool steep = ABS(y1 - y0) > ABS(x1 - x0);
    if (steep) {
        _swap_int16_t(x0, y0);
        _swap_int16_t(x1, y1);
    }

    if (x0 > x1) {
        _swap_int16_t(x0, x1);
        _swap_int16_t(y0, y1);
    }

    int16_t dx = x1 - x0, dy = ABS(y1 - y0);
    int16_t err = dx >> 1, ystep = -1, xs = x0, dlen = 0;

    if (y0 < y1) {
        ystep = 1;
    }

    // Split into steep and not steep for FastH/V separation
    if (steep) {
        for (; x0 <= x1; x0++) {
            dlen++;
            err -= dy;
            if (err < 0) {
                err += dx;
                fast_vline(self, y0, xs, dlen, color);
                dlen = 0;
                y0 += ystep;
                xs = x0 + 1;
            }
        }
        if (dlen) {
            fast_vline(self, y0, xs, dlen, color);
        }
    } else {
        for (; x0 <= x1; x0++) {
            dlen++;
            err -= dy;
            if (err < 0) {
                err += dx;
                fast_hline(self, xs, y0, dlen, color);
                dlen = 0;
                y0 += ystep;
                xs = x0 + 1;
            }
        }
        if (dlen) {
            fast_hline(self, xs, y0, dlen, color);
        }
    }
}


STATIC mp_obj_t rm67162_RM67162_line(size_t n_args, const mp_obj_t *args_in) {
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint16_t x0 = mp_obj_get_int(args_in[1]);
    uint16_t y0 = mp_obj_get_int(args_in[2]);
    uint16_t x1 = mp_obj_get_int(args_in[3]);
    uint16_t y1 = mp_obj_get_int(args_in[4]);
    uint16_t color = mp_obj_get_int(args_in[5]);

    line(self, x0, y0, x1, y1, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_line_obj, 6, 6, rm67162_RM67162_line);


STATIC mp_obj_t rm67162_RM67162_bitmap(size_t n_args, const mp_obj_t *args_in) {
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);

    int x_start = mp_obj_get_int(args_in[1]);
    int y_start = mp_obj_get_int(args_in[2]);
    int x_end   = mp_obj_get_int(args_in[3]);
    int y_end   = mp_obj_get_int(args_in[4]);

    x_start += self->x_gap;
    x_end += self->x_gap;
    y_start += self->y_gap;
    y_end += self->y_gap;

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args_in[5], &bufinfo, MP_BUFFER_READ);
    set_area(self, x_start, y_start, x_end, y_end);
    size_t len = ((x_end - x_start) * (y_end - y_start) * self->fb_bpp / 8);
    write_color(self, bufinfo.buf, len);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_bitmap_obj, 6, 6, rm67162_RM67162_bitmap);


STATIC mp_obj_t rm67162_RM67162_text(size_t n_args, const mp_obj_t *args) {
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint8_t single_char_s;
    const uint8_t *source = NULL;
    size_t source_len = 0;

    // extract arguments
    mp_obj_module_t *font = MP_OBJ_TO_PTR(args[1]);

    if (mp_obj_is_int(args[2])) {
        mp_int_t c = mp_obj_get_int(args[2]);
        single_char_s = (c & 0xff);
        source = &single_char_s;
        source_len = 1;
    } else if (mp_obj_is_str(args[2])) {
        source = (uint8_t *) mp_obj_str_get_str(args[2]);
        source_len = strlen((char *)source);
    } else if (mp_obj_is_type(args[2], &mp_type_bytes)) {
        mp_obj_t text_data_buff = args[2];
        mp_buffer_info_t text_bufinfo;
        mp_get_buffer_raise(text_data_buff, &text_bufinfo, MP_BUFFER_READ);
        source = text_bufinfo.buf;
        source_len = text_bufinfo.len;
    } else {
        mp_raise_TypeError(MP_ERROR_TEXT("text requires either int, str or bytes."));
        return mp_const_none;
    }

    mp_int_t x0 = mp_obj_get_int(args[3]);
    mp_int_t y0 = mp_obj_get_int(args[4]);

    mp_obj_dict_t *dict = MP_OBJ_TO_PTR(font->globals);
    const uint8_t width = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_WIDTH)));
    const uint8_t height = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_HEIGHT)));
    const uint8_t first = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_FIRST)));
    const uint8_t last = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_LAST)));

    mp_obj_t font_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_FONT));
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(font_data_buff, &bufinfo, MP_BUFFER_READ);
    const uint8_t *font_data = bufinfo.buf;

    uint16_t fg_color;
    uint16_t bg_color;

    if (n_args > 5) {
        fg_color = mp_obj_get_int(args[5]);
    } else {
        fg_color = WHITE;
    }

    if (n_args > 6) {
        bg_color = mp_obj_get_int(args[6]);
    } else {
        bg_color = BLACK;
    }

    uint8_t wide = width / 8;
    size_t buf_size = width * height * 2;

    uint8_t chr;
    if (self->frame_buffer) {
    while (source_len--) {
        chr = *source++;
            if (chr >= first && chr <= last) {
                uint16_t buf_idx = 0;
                uint16_t chr_idx = (chr - first) * (height * wide);
                for (uint8_t line = 0; line < height; line++) {
                    for (uint8_t line_byte = 0; line_byte < wide; line_byte++) {
                        uint8_t chr_data = font_data[chr_idx];
                        for (uint8_t bit = 8; bit; bit--) {
                            if (chr_data >> (bit - 1) & 1) {
                                self->frame_buffer[buf_idx] = fg_color;
                            } else {
                                self->frame_buffer[buf_idx] = bg_color;
                            }
                            buf_idx++;
                        }
                        chr_idx++;
                    }
                }
                uint16_t x1 = x0 + width - 1;
                if (x1 < self->width) {
                    set_area(self, x0, y0, x1, y0 + height - 1);
                    write_color(self, (uint8_t *)self->frame_buffer, buf_size);
                }
                x0 += width;
            }
        }
    }
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_text_obj, 5, 7, rm67162_RM67162_text);

STATIC uint32_t bs_bit = 0;
uint8_t *bitmap_data = NULL;

STATIC uint8_t get_color(uint8_t bpp) {
    uint8_t color = 0;
    int i;

    for (i = 0; i < bpp; i++) {
        color <<= 1;
        color |= (bitmap_data[bs_bit / 8] & 1 << (7 - (bs_bit % 8))) > 0;
        bs_bit++;
    }
    return color;
}

STATIC mp_obj_t rm67162_RM67162_write_len(size_t n_args, const mp_obj_t *args) {
    mp_obj_module_t *font = MP_OBJ_TO_PTR(args[1]);
    mp_obj_dict_t *dict = MP_OBJ_TO_PTR(font->globals);
    mp_obj_t widths_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_WIDTHS));
    mp_buffer_info_t widths_bufinfo;
    mp_get_buffer_raise(widths_data_buff, &widths_bufinfo, MP_BUFFER_READ);
    const uint8_t *widths_data = widths_bufinfo.buf;

    uint16_t print_width = 0;

    mp_obj_t map_obj = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_MAP));
    GET_STR_DATA_LEN(map_obj, map_data, map_len);
    GET_STR_DATA_LEN(args[2], str_data, str_len);
    const byte *s = str_data, *top = str_data + str_len;

    while (s < top) {
        unichar ch;
        ch = utf8_get_char(s);
        s = utf8_next_char(s);

        const byte *map_s = map_data, *map_top = map_data + map_len;
        uint16_t char_index = 0;

        while (map_s < map_top) {
            unichar map_ch;
            map_ch = utf8_get_char(map_s);
            map_s = utf8_next_char(map_s);

            if (ch == map_ch) {
                print_width += widths_data[char_index];
                break;
            }
            char_index++;
        }
    }

    return mp_obj_new_int(print_width);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_write_len_obj, 3, 3, rm67162_RM67162_write_len);

//
//	write(font_module, s, x, y[, fg, bg, background_tuple, fill])
//		background_tuple (bitmap_buffer, width, height)
//

STATIC mp_obj_t rm67162_RM67162_write(size_t n_args, const mp_obj_t *args) {
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_obj_module_t *font = MP_OBJ_TO_PTR(args[1]);

    mp_int_t x = mp_obj_get_int(args[3]);
    mp_int_t y = mp_obj_get_int(args[4]);
    mp_int_t fg_color;
    mp_int_t bg_color;

    fg_color = (n_args > 5) ? mp_obj_get_int(args[5]) : WHITE;
    bg_color = (n_args > 6) ? mp_obj_get_int(args[6]) : BLACK;

    mp_obj_t *tuple_data = NULL;
    size_t tuple_len = 0;

    mp_buffer_info_t background_bufinfo;
    uint16_t background_width = 0;
    uint16_t background_height = 0;
    uint16_t *background_data = NULL;

    if (n_args > 7) {
        mp_obj_tuple_get(args[7], &tuple_len, &tuple_data);
        if (tuple_len > 2) {
            mp_get_buffer_raise(tuple_data[0], &background_bufinfo, MP_BUFFER_READ);
            background_data = background_bufinfo.buf;
            background_width = mp_obj_get_int(tuple_data[1]);
            background_height = mp_obj_get_int(tuple_data[2]);
        }
    }

    bool fill = (n_args > 8) ? mp_obj_is_true(args[8]) : false;

    mp_obj_dict_t *dict = MP_OBJ_TO_PTR(font->globals);
    const uint8_t bpp = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_BPP)));
    const uint8_t height = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_HEIGHT)));
    const uint8_t offset_width = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_OFFSET_WIDTH)));
    const uint8_t max_width = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_MAX_WIDTH)));

    mp_obj_t widths_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_WIDTHS));
    mp_buffer_info_t widths_bufinfo;
    mp_get_buffer_raise(widths_data_buff, &widths_bufinfo, MP_BUFFER_READ);
    const uint8_t *widths_data = widths_bufinfo.buf;

    mp_obj_t offsets_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_OFFSETS));
    mp_buffer_info_t offsets_bufinfo;
    mp_get_buffer_raise(offsets_data_buff, &offsets_bufinfo, MP_BUFFER_READ);
    const uint8_t *offsets_data = offsets_bufinfo.buf;

    mp_obj_t bitmaps_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_BITMAPS));
    mp_buffer_info_t bitmaps_bufinfo;
    mp_get_buffer_raise(bitmaps_data_buff, &bitmaps_bufinfo, MP_BUFFER_READ);
    bitmap_data = bitmaps_bufinfo.buf;

    // if fill is set, and background bitmap data is available copy the background
    // bitmap data into the buffer. The background buffer must be the size of the
    // widest character in the font.

    if (fill && background_data && self->frame_buffer) {
        memcpy(self->frame_buffer, background_data, background_width * background_height * 2);
    }

    uint16_t print_width = 0;
    mp_obj_t map_obj = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_MAP));
    GET_STR_DATA_LEN(map_obj, map_data, map_len);
    GET_STR_DATA_LEN(args[2], str_data, str_len);
    const byte *s = str_data, *top = str_data + str_len;
    while (s < top) {
        unichar ch;
        ch = utf8_get_char(s);
        s = utf8_next_char(s);

        const byte *map_s = map_data, *map_top = map_data + map_len;
        uint16_t char_index = 0;

        while (map_s < map_top) {
            unichar map_ch;
            map_ch = utf8_get_char(map_s);
            map_s = utf8_next_char(map_s);

            if (ch == map_ch) {
                uint8_t width = widths_data[char_index];

                bs_bit = 0;
                switch (offset_width) {
                    case 1:
                        bs_bit = offsets_data[char_index * offset_width];
                        break;

                    case 2:
                        bs_bit = (offsets_data[char_index * offset_width] << 8) +
                            (offsets_data[char_index * offset_width + 1]);
                        break;

                    case 3:
                        bs_bit = (offsets_data[char_index * offset_width] << 16) +
                            (offsets_data[char_index * offset_width + 1] << 8) +
                            (offsets_data[char_index * offset_width + 2]);
                        break;
                }

                uint16_t buffer_width = (fill) ? max_width : width;

                uint16_t color = 0;
                for (uint16_t yy = 0; yy < height; yy++) {
                    for (uint16_t xx = 0; xx < width; xx++) {
                        if (background_data && (xx <= background_width && yy <= background_height)) {
                            if (get_color(bpp) == bg_color) {
                                color = background_data[(yy * background_width + xx)];
                            } else {
                                color = fg_color;
                            }
                        } else {
                            color = get_color(bpp) ? fg_color : bg_color;
                        }
                        self->frame_buffer[yy * buffer_width + xx] = color;
                    }
                }

                uint32_t data_size = buffer_width * height * 2;
                uint16_t x2 = x + buffer_width - 1;
                uint16_t y2 = y + height - 1;
                if (x2 < self->width) {
                    set_area(self, x, y, x2, y2);
                    write_color(self, (uint8_t *)self->frame_buffer, data_size);
                    print_width += width;
                }
                x += width;
                break;
            }
            char_index++;
        }
    }

    return mp_obj_new_int(print_width);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_write_obj, 5, 9, rm67162_RM67162_write);


/*---------------------------------------------------------------------------------------------------
Below are screencontroler related functions
----------------------------------------------------------------------------------------------------*/


STATIC mp_obj_t rm67162_RM67162_mirror(mp_obj_t self_in,
                                      mp_obj_t mirror_x_in,
                                      mp_obj_t mirror_y_in)
{
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (mp_obj_is_true(mirror_x_in)) {
        self->madctl_val |= (1 << 6);
    } else {
        self->madctl_val &= ~(1 << 6);
    }
    if (mp_obj_is_true(mirror_y_in)) {
        self->madctl_val |= (1 << 7);
    } else {
        self->madctl_val &= ~(1 << 7);
    }

    write_spi(self, LCD_CMD_MADCTL, (uint8_t[]) {
            self->madctl_val
    }, 1);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(rm67162_RM67162_mirror_obj, rm67162_RM67162_mirror);


STATIC mp_obj_t rm67162_RM67162_swap_xy(mp_obj_t self_in, mp_obj_t swap_axes_in)
{
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (mp_obj_is_true(swap_axes_in)) {
        self->madctl_val |= 1 << 5;
    } else {
        self->madctl_val &= ~(1 << 5);
    }

    write_spi(self, LCD_CMD_MADCTL, (uint8_t[]) {
            self->madctl_val
    }, 1);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(rm67162_RM67162_swap_xy_obj, rm67162_RM67162_swap_xy);


STATIC mp_obj_t rm67162_RM67162_set_gap(mp_obj_t self_in,
                                       mp_obj_t x_gap_in,
                                       mp_obj_t y_gap_in)
{
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(self_in);

    self->x_gap = mp_obj_get_int(x_gap_in);
    self->y_gap = mp_obj_get_int(y_gap_in);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(rm67162_RM67162_set_gap_obj, rm67162_RM67162_set_gap);


STATIC mp_obj_t rm67162_RM67162_invert_color(mp_obj_t self_in, mp_obj_t invert_in)
{
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (mp_obj_is_true(invert_in)) {
        write_spi(self, LCD_CMD_INVON, NULL, 0);
    } else {
        write_spi(self, LCD_CMD_INVOFF, NULL, 0);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(rm67162_RM67162_invert_color_obj, rm67162_RM67162_invert_color);


STATIC mp_obj_t rm67162_RM67162_disp_off(mp_obj_t self_in)
{
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(self_in);

    write_spi(self, LCD_CMD_SLPIN, NULL, 0);
    write_spi(self, LCD_CMD_DISPOFF, NULL, 0);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(rm67162_RM67162_disp_off_obj, rm67162_RM67162_disp_off);


STATIC mp_obj_t rm67162_RM67162_disp_on(mp_obj_t self_in)
{
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(self_in);

    write_spi(self, LCD_CMD_SLPOUT, NULL, 0);
    write_spi(self, LCD_CMD_DISPON, NULL, 0);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(rm67162_RM67162_disp_on_obj, rm67162_RM67162_disp_on);


STATIC mp_obj_t rm67162_RM67162_backlight_on(mp_obj_t self_in)
{
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(self_in);

    write_spi(self, LCD_CMD_WRDISBV, (uint8_t[]) {
            0XFF
    }, 1);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(rm67162_RM67162_backlight_on_obj, rm67162_RM67162_backlight_on);


STATIC mp_obj_t rm67162_RM67162_backlight_off(mp_obj_t self_in)
{
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(self_in);

    write_spi(self, LCD_CMD_WRDISBV, (uint8_t[]) {
            0x00
    }, 1);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(rm67162_RM67162_backlight_off_obj, rm67162_RM67162_backlight_off);


STATIC mp_obj_t rm67162_RM67162_brightness(mp_obj_t self_in, mp_obj_t brightness_in)
{
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t brightness = mp_obj_get_int(brightness_in);

    if (brightness > 100) {
        brightness = 100;
    } else if (brightness < 0) {
        brightness = 0;
    }

    write_spi(self, LCD_CMD_WRDISBV, (uint8_t[]) {
            (brightness * 255 / 100) & 0xFF
    }, 1);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(rm67162_RM67162_brightness_obj, rm67162_RM67162_brightness);


STATIC mp_obj_t rm67162_RM67162_width(mp_obj_t self_in)
{
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->width);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(rm67162_RM67162_width_obj, rm67162_RM67162_width);


STATIC mp_obj_t rm67162_RM67162_height(mp_obj_t self_in)
{
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->height);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(rm67162_RM67162_height_obj, rm67162_RM67162_height);


STATIC mp_obj_t rm67162_RM67162_rotation(size_t n_args, const mp_obj_t *args_in)
{
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    self->rotation = mp_obj_get_int(args_in[1]) % 4;
    if (n_args > 2) {
        mp_obj_tuple_t *rotations_in = MP_OBJ_TO_PTR(args_in[2]);
        for (size_t i = 0; i < rotations_in->len; i++) {
            if (i < 4) {
                mp_obj_tuple_t *item = MP_OBJ_TO_PTR(rotations_in->items[i]);
                self->rotations[i].madctl   = mp_obj_get_int(item->items[0]);
                self->rotations[i].width    = mp_obj_get_int(item->items[1]);
                self->rotations[i].height   = mp_obj_get_int(item->items[2]);
                self->rotations[i].colstart = mp_obj_get_int(item->items[3]);
                self->rotations[i].rowstart = mp_obj_get_int(item->items[4]);
            }
        }
    }
    set_rotation(self, self->rotation);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_rotation_obj, 2, 3, rm67162_RM67162_rotation);


STATIC mp_obj_t rm67162_RM67162_vscroll_area(size_t n_args, const mp_obj_t *args_in)
{
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_int_t tfa = mp_obj_get_int(args_in[1]);
    mp_int_t vsa = mp_obj_get_int(args_in[2]);
    mp_int_t bfa = mp_obj_get_int(args_in[3]);

    write_spi(
            self,
            LCD_CMD_VSCRDEF,
            (uint8_t []) {
                (tfa) >> 8,
                (tfa) & 0xFF,
                (vsa) >> 8,
                (vsa) & 0xFF,
                (bfa) >> 8,
                (bfa) & 0xFF
            },
            6
    );

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_vscroll_area_obj, 4, 4, rm67162_RM67162_vscroll_area);


STATIC mp_obj_t rm67162_RM67162_vscroll_start(size_t n_args, const mp_obj_t *args_in)
{
    rm67162_RM67162_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_int_t vssa = mp_obj_get_int(args_in[1]);

    if (n_args > 2) {
        if (mp_obj_is_true(args_in[2])) {
            self->madctl_val |= LCD_CMD_ML_BIT;
        } else {
            self->madctl_val &= ~LCD_CMD_ML_BIT;
        }
    } else {
        self->madctl_val &= ~LCD_CMD_ML_BIT;
    }
    write_spi(
        self,
        LCD_CMD_MADCTL,
        (uint8_t[]) { self->madctl_val, },
        2
    );

    write_spi(
        self,
        LCD_CMD_VSCSAD,
        (uint8_t []) { (vssa) >> 8, (vssa) & 0xFF },
        2
    );

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rm67162_RM67162_vscroll_start_obj, 2, 3, rm67162_RM67162_vscroll_start);


/*
Mapping to Micropython
*/


STATIC const mp_rom_map_elem_t rm67162_RM67162_locals_dict_table[] = {
    /* { MP_ROM_QSTR(MP_QSTR_custom_init),   MP_ROM_PTR(&rm67162_RM67162_custom_init_obj)   }, */
    { MP_ROM_QSTR(MP_QSTR_deinit),          MP_ROM_PTR(&rm67162_RM67162_deinit_obj)          },
    { MP_ROM_QSTR(MP_QSTR_reset),           MP_ROM_PTR(&rm67162_RM67162_reset_obj)           },
    { MP_ROM_QSTR(MP_QSTR_init),            MP_ROM_PTR(&rm67162_RM67162_init_obj)            },
    { MP_ROM_QSTR(MP_QSTR_send_cmd),        MP_ROM_PTR(&rm67162_RM67162_send_cmd_obj)        },
    { MP_ROM_QSTR(MP_QSTR_pixel),           MP_ROM_PTR(&rm67162_RM67162_pixel_obj)           },
    {MP_ROM_QSTR(MP_QSTR_write_len),        MP_ROM_PTR(&rm67162_RM67162_write_len_obj)},
    {MP_ROM_QSTR(MP_QSTR_write),            MP_ROM_PTR(&rm67162_RM67162_write_obj)},
    { MP_ROM_QSTR(MP_QSTR_hline),           MP_ROM_PTR(&rm67162_RM67162_hline_obj)           },
    { MP_ROM_QSTR(MP_QSTR_vline),           MP_ROM_PTR(&rm67162_RM67162_vline_obj)           },
    { MP_ROM_QSTR(MP_QSTR_fill),            MP_ROM_PTR(&rm67162_RM67162_fill_obj)            },
    { MP_ROM_QSTR(MP_QSTR_fill_rect),       MP_ROM_PTR(&rm67162_RM67162_fill_rect_obj)       },
    { MP_ROM_QSTR(MP_QSTR_fill_bubble_rect),MP_ROM_PTR(&rm67162_RM67162_fill_bubble_rect_obj)},
    { MP_ROM_QSTR(MP_QSTR_fill_circle),     MP_ROM_PTR(&rm67162_RM67162_fill_circle_obj)     },
    { MP_ROM_QSTR(MP_QSTR_line),            MP_ROM_PTR(&rm67162_RM67162_line_obj)            },
    { MP_ROM_QSTR(MP_QSTR_rect),            MP_ROM_PTR(&rm67162_RM67162_rect_obj)            },
    { MP_ROM_QSTR(MP_QSTR_bubble_rect),     MP_ROM_PTR(&rm67162_RM67162_bubble_rect_obj)     },
    { MP_ROM_QSTR(MP_QSTR_circle),          MP_ROM_PTR(&rm67162_RM67162_circle_obj)          },
    { MP_ROM_QSTR(MP_QSTR_colorRGB),        MP_ROM_PTR(&rm67162_RM67162_colorRGB_obj)        },
    { MP_ROM_QSTR(MP_QSTR_bitmap),          MP_ROM_PTR(&rm67162_RM67162_bitmap_obj)          },
    { MP_ROM_QSTR(MP_QSTR_text),            MP_ROM_PTR(&rm67162_RM67162_text_obj)            },
    { MP_ROM_QSTR(MP_QSTR_mirror),          MP_ROM_PTR(&rm67162_RM67162_mirror_obj)          },
    { MP_ROM_QSTR(MP_QSTR_swap_xy),         MP_ROM_PTR(&rm67162_RM67162_swap_xy_obj)         },
    { MP_ROM_QSTR(MP_QSTR_set_gap),         MP_ROM_PTR(&rm67162_RM67162_set_gap_obj)         },
    { MP_ROM_QSTR(MP_QSTR_invert_color),    MP_ROM_PTR(&rm67162_RM67162_invert_color_obj)    },
    { MP_ROM_QSTR(MP_QSTR_disp_off),        MP_ROM_PTR(&rm67162_RM67162_disp_off_obj)        },
    { MP_ROM_QSTR(MP_QSTR_disp_on),         MP_ROM_PTR(&rm67162_RM67162_disp_on_obj)         },
    { MP_ROM_QSTR(MP_QSTR_backlight_on),    MP_ROM_PTR(&rm67162_RM67162_backlight_on_obj)    },
    { MP_ROM_QSTR(MP_QSTR_backlight_off),   MP_ROM_PTR(&rm67162_RM67162_backlight_off_obj)   },
    { MP_ROM_QSTR(MP_QSTR_brightness),      MP_ROM_PTR(&rm67162_RM67162_brightness_obj)      },
    { MP_ROM_QSTR(MP_QSTR_height),          MP_ROM_PTR(&rm67162_RM67162_height_obj)          },
    { MP_ROM_QSTR(MP_QSTR_width),           MP_ROM_PTR(&rm67162_RM67162_width_obj)           },
    { MP_ROM_QSTR(MP_QSTR_rotation),        MP_ROM_PTR(&rm67162_RM67162_rotation_obj)        },
    { MP_ROM_QSTR(MP_QSTR_vscroll_area),    MP_ROM_PTR(&rm67162_RM67162_vscroll_area_obj)    },
    { MP_ROM_QSTR(MP_QSTR_vscroll_start),   MP_ROM_PTR(&rm67162_RM67162_vscroll_start_obj)   },
    { MP_ROM_QSTR(MP_QSTR___del__),         MP_ROM_PTR(&rm67162_RM67162_deinit_obj)          },
    { MP_ROM_QSTR(MP_QSTR_RGB),             MP_ROM_INT(COLOR_SPACE_RGB)                      },
    { MP_ROM_QSTR(MP_QSTR_BGR),             MP_ROM_INT(COLOR_SPACE_BGR)                      },
    { MP_ROM_QSTR(MP_QSTR_MONOCHROME),      MP_ROM_INT(COLOR_SPACE_MONOCHROME)               },
};
STATIC MP_DEFINE_CONST_DICT(rm67162_RM67162_locals_dict, rm67162_RM67162_locals_dict_table);


#ifdef MP_OBJ_TYPE_GET_SLOT
MP_DEFINE_CONST_OBJ_TYPE(
    rm67162_RM67162_type,
    MP_QSTR_RM67162,
    MP_TYPE_FLAG_NONE,
    print, rm67162_RM67162_print,
    make_new, rm67162_RM67162_make_new,
    locals_dict, (mp_obj_dict_t *)&rm67162_RM67162_locals_dict
);
#else
const mp_obj_type_t rm67162_RM67162_type = {
    { &mp_type_type },
    .name        = MP_QSTR_RM67162,
    .print       = rm67162_RM67162_print,
    .make_new    = rm67162_RM67162_make_new,
    .locals_dict = (mp_obj_dict_t *)&rm67162_RM67162_locals_dict,
};
#endif


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
