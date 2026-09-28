#include "ssd1306.h"
#include <string.h>


/* ============================================================
   SSD1306 state
   ============================================================ */

static I2C_HandleTypeDef *oledI2C = nullptr;

static uint8_t oledBuffer[
    SSD1306_WIDTH *
    SSD1306_HEIGHT / 8
];

static uint8_t cursorX = 0;
static uint8_t cursorPage = 0;


/* ============================================================
   Small 5x7 font

   Supports:
   space
   0-9
   A-Z
   :
   .
   -
   %
   ============================================================ */

static const uint8_t FONT_SPACE[5] =
{
    0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t FONT_COLON[5] =
{
    0x00, 0x36, 0x36, 0x00, 0x00
};

static const uint8_t FONT_DOT[5] =
{
    0x00, 0x60, 0x60, 0x00, 0x00
};

static const uint8_t FONT_MINUS[5] =
{
    0x08, 0x08, 0x08, 0x08, 0x08
};

static const uint8_t FONT_PERCENT[5] =
{
    0x63, 0x13, 0x08, 0x64, 0x63
};


static const uint8_t FONT_DIGITS[10][5] =
{
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 1 */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 3 */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 4 */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 6 */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
    {0x06, 0x49, 0x49, 0x29, 0x1E}  /* 9 */
};


static const uint8_t FONT_LETTERS[26][5] =
{
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* A */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* B */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* C */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* D */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* E */
    {0x7F, 0x09, 0x09, 0x09, 0x01}, /* F */
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, /* G */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* H */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* I */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* J */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* K */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* L */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, /* M */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* N */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* O */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* P */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* Q */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* R */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* S */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* T */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* U */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* V */
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, /* W */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* X */
    {0x07, 0x08, 0x70, 0x08, 0x07}, /* Y */
    {0x61, 0x51, 0x49, 0x45, 0x43}  /* Z */
};


/* ============================================================
   Send SSD1306 command
   ============================================================ */

static bool SSD1306_WriteCommand(uint8_t command)
{
    uint8_t packet[2];

    packet[0] = 0x00;
    packet[1] = command;

    return (
        HAL_I2C_Master_Transmit(
            oledI2C,
            SSD1306_ADDRESS << 1,
            packet,
            sizeof(packet),
            100
        ) == HAL_OK
    );
}


/* ============================================================
   Find glyph
   ============================================================ */

static const uint8_t *GetGlyph(char c)
{
    if (c == ' ')
    {
        return FONT_SPACE;
    }

    if (c >= '0' && c <= '9')
    {
        return FONT_DIGITS[c - '0'];
    }

    if (c >= 'a' && c <= 'z')
    {
        c -= ('a' - 'A');
    }

    if (c >= 'A' && c <= 'Z')
    {
        return FONT_LETTERS[c - 'A'];
    }

    if (c == ':')
    {
        return FONT_COLON;
    }

    if (c == '.')
    {
        return FONT_DOT;
    }

    if (c == '-')
    {
        return FONT_MINUS;
    }

    if (c == '%')
    {
        return FONT_PERCENT;
    }

    return FONT_SPACE;
}


/* ============================================================
   Initialize SSD1306
   ============================================================ */

bool SSD1306_Init(I2C_HandleTypeDef *hi2c)
{
    oledI2C = hi2c;

    if (oledI2C == nullptr)
    {
        return false;
    }

    HAL_Delay(100);

    const uint8_t commands[] =
    {
        0xAE,       /* Display OFF */

        0xD5,
        0x80,       /* Clock divide */

        0xA8,
        0x3F,       /* Multiplex 1/64 */

        0xD3,
        0x00,       /* Display offset */

        0x40,       /* Start line */

        0x8D,
        0x14,       /* Charge pump ON */

        0x20,
        0x00,       /* Horizontal addressing */

        0xA1,       /* Segment remap */

        0xC8,       /* COM scan direction */

        0xDA,
        0x12,       /* COM pins */

        0x81,
        0x7F,       /* Contrast */

        0xD9,
        0xF1,       /* Pre-charge */

        0xDB,
        0x40,       /* VCOM detect */

        0xA4,       /* Resume RAM display */

        0xA6,       /* Normal display */

        0x2E,       /* Disable scrolling */

        0xAF        /* Display ON */
    };

    for (
        uint32_t i = 0;
        i < sizeof(commands);
        i++
    )
    {
        if (!SSD1306_WriteCommand(commands[i]))
        {
            return false;
        }
    }

    SSD1306_Clear();
    SSD1306_UpdateScreen();

    return true;
}


/* ============================================================
   Clear framebuffer
   ============================================================ */

void SSD1306_Clear(void)
{
    memset(
        oledBuffer,
        0,
        sizeof(oledBuffer)
    );

    cursorX = 0;
    cursorPage = 0;
}


/* ============================================================
   Set text cursor

   page = 0 to 7
   ============================================================ */

void SSD1306_SetCursor(
    uint8_t x,
    uint8_t page
)
{
    if (x >= SSD1306_WIDTH)
    {
        x = 0;
    }

    if (page >= 8)
    {
        page = 0;
    }

    cursorX = x;
    cursorPage = page;
}


/* ============================================================
   Draw character into framebuffer
   ============================================================ */

void SSD1306_WriteChar(char c)
{
    const uint8_t *glyph =
        GetGlyph(c);

    if ((cursorX + 6) >= SSD1306_WIDTH)
    {
        cursorX = 0;

        if (cursorPage < 7)
        {
            cursorPage++;
        }
    }

    uint16_t offset =
        ((uint16_t)cursorPage *
         SSD1306_WIDTH)
        + cursorX;

    for (uint8_t i = 0; i < 5; i++)
    {
        oledBuffer[offset + i] =
            glyph[i];
    }

    oledBuffer[offset + 5] =
        0x00;

    cursorX += 6;
}


/* ============================================================
   Draw string
   ============================================================ */

void SSD1306_WriteString(
    const char *text
)
{
    if (text == nullptr)
    {
        return;
    }

    while (*text)
    {
        SSD1306_WriteChar(
            *text
        );

        text++;
    }
}


/* ============================================================
   Send framebuffer to OLED
   ============================================================ */

void SSD1306_UpdateScreen(void)
{
    if (oledI2C == nullptr)
    {
        return;
    }

    /* Full column range */

    SSD1306_WriteCommand(
        0x21
    );

    SSD1306_WriteCommand(
        0
    );

    SSD1306_WriteCommand(
        SSD1306_WIDTH - 1
    );


    /* Full page range */

    SSD1306_WriteCommand(
        0x22
    );

    SSD1306_WriteCommand(
        0
    );

    SSD1306_WriteCommand(
        7
    );


    /*
     * Send one page at a time.
     *
     * byte 0 = 0x40 means following bytes
     * are display RAM data.
     */

    uint8_t packet[
        SSD1306_WIDTH + 1
    ];

    packet[0] = 0x40;


    for (uint8_t page = 0; page < 8; page++)
    {
        memcpy(
            &packet[1],
            &oledBuffer[
                page * SSD1306_WIDTH
            ],
            SSD1306_WIDTH
        );


        HAL_I2C_Master_Transmit(
            oledI2C,
            SSD1306_ADDRESS << 1,
            packet,
            sizeof(packet),
            200
        );
    }
}