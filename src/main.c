

#ifdef IDE
#undef __SDCC_mcs51
#include <mcs51/lint.h>
#define __asm
#define __endasm
#define nop asm("nop");
#endif

#include <mcs51/8052.h>
#include <mcs51reg.h>

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>



__xdata __at(0x0000C000) uint8_t _lcd_instruction;
__xdata __at(0x0000C001) uint8_t _lcd_data;

__xdata __at(0x0000C002) uint8_t _lcd_rd_instruction;
__xdata __at(0x0000C003) uint8_t _lcd_rd_data;


__xdata __at(0x00008004) uint8_t _8004;
__xdata __at(0x00008005) uint8_t _8005;
__xdata __at(0x00008006) uint8_t _8006;

__xdata __at(0x00008007) uint8_t _backlight;

__xdata __at(0x00000000) uint8_t _0000[];
__xdata __at(0x0000C000) uint8_t _c000[];
__xdata __at(0x00008000) uint8_t _8000[];


/* 16 bit timer, 12 clock per instr: 65536 - (20ms * CLK / 12) */
#define T0_RELOAD (65536 - (F_CPU / (20 * 12)))
#define T0_RELOAD_H (T0_RELOAD / 256)
#define T0_RELOAD_L (T0_RELOAD % 256)

/* pet the external watchdog */
void isr_timer0(void) __interrupt(TF0_VECTOR)
{
    TH0 = T0_RELOAD_H;
    TL0 = T0_RELOAD_L;

    P1_4 = !P1_4;
}

int getchar()
{
    char c;
    while (!RI)
        ;
    RI = 0;
    c = SBUF;
    return c;
}

int putchar(int c)
{
    while (!TI)
        ;
    TI = 0;
    SBUF = c;
    if (c == '\n')
        putchar('\r');

    return (c);
}

static inline bool kbhit(void)
{
    return RI;
}

char hex2num(char hex)
{
    if (hex >= 'A')
    {
        if (hex >= 'a')
            hex -= 'a' - 'A'; // tolower
        hex -= 'A' - '9' - 1; // todigit
    }
    return hex - '0'; // tohex
}

uint16_t readhex(bool is_16)
{
    uint16_t hex = 0;
    for (uint8_t i = 0; i < (is_16 ? 4 : 2);)
    {
        char input = getchar();
        if (input == '\n')
            return hex;
        if (((input >= '0') && (input <= '9')) ||
            ((input >= 'A') && (input <= 'F')) ||
            ((input >= 'a') && (input <= 'f')))
        {
            uint8_t rd_hex = hex2num(input);
            hex <<= 4;
            hex |= rd_hex;
            i++;
        }
        else
        {
            continue;
        }
    }

    return hex;
}

static void delay_ms(uint16_t ms)
{
    uint8_t inner;
    while (ms--)
    {
        /* value tweaked by hand on the hardware */
        inner = 110;
        while (inner--)
            ;
    }
}

static bool lcd_busy(void)
{
    return _lcd_rd_instruction & 0x80 ? true : false;
}

static void lcd_init(void)
{
    _lcd_instruction = 0x30;
    delay_ms(4);
    _lcd_instruction = 0x30;
    delay_ms(4);
    _lcd_instruction = 0x30;
    while (lcd_busy())
        ;

    /* Func set: 8 bits, Two line, 5x8 font */
    _lcd_instruction = 0x38;
    while (lcd_busy())
        ;

    /* Display on, cursor off, cursor not blinking */
    _lcd_instruction = 0xc;
    while (lcd_busy())
        ;

    /* Clear display */
    _lcd_instruction = 0x1;
    while (lcd_busy())
        ;

    /* Entry mode: cursor moves to the right, no display shifting */
    _lcd_instruction = 0x6;
    while (lcd_busy())
        ;
}

static void lcd_gotoxy(uint8_t x, uint8_t y)
{
    uint8_t addr;
    if (y >= 20)
        return;
    switch (x)
    {
    case 0:
        addr = 0x00;
        break;
    case 1:
        addr = 0x28;
        break;
    case 2:
        addr = 0x14;
        break;
    case 3:
        addr = 0x3C;
        break;
    default:
        return;
    }
    addr += y;
    _lcd_instruction = 0x80 | addr;
}

static void lcd_putchar(char c)
{
    while (lcd_busy())
        ;

    _lcd_data = c;
}

static void lcd_puts(const char *string)
{
    while (*string)
    {
        lcd_putchar(*string);
        string++;
    }
}

static void lcd_set_contrast(uint8_t contrast)
{
    uint8_t temp = ~(contrast) + 8;

    _8004 = temp & 1;

    _8005 = (contrast >> 1) & 1;

    _8006 = temp >> 2;
}

/*
 * I2C stuff from https://gist.github.com/jasmill/de55635c4ee1e544b0014fbb7a882cd1
 */

#define SCL P1_6 // I2C Clock
#define SDA P1_7 // I2C Data

#define EEPROM_ID 0xA0 // Device identifier of the eeprom (24C16)
#define READ 0x01      // Alias for read command bit
#define WRITE 0x00     // Alias for write command bit

void i2c_delay(void)
{

    EA = 0; // Disable all interrupts for an accurate timing

    __asm
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
    __endasm;

    EA = 1; // Enable all interrupts
}

void i2c_start(void)
{

    EA = 0; // Disable all interrupts for a accurate timing

    // Force defined state of SCL and SDA
    SCL = 1;     // Release SCL
    SDA = 1;     // Release SDA
    i2c_delay(); // Force 5µs-delay

    // Generate START condition
    SDA = 0;
    i2c_delay(); // Force 5µs-delay
    SCL = 0;

    EA = 1; // Enable all interrupts

    // I2C Busy

    return;
}

void i2c_stop(void)
{

    EA = 0; // Disable all interrupts for a accurate timing

    // Generate STOP condition
    SDA = 0;
    i2c_delay(); // Force 5µs-delay
    SCL = 1;     // Release SCL
    i2c_delay(); // Force 5µs-delay
    SDA = 1;     // Release SDA

    EA = 1; // Enable all interrupts

    // I2C Idle

    return;
}

void i2c_ack(void)
{

    EA = 0; // Disable all interrupts for a accurate timing

    // Generate ACK
    SDA = 0;
    SCL = 1; // Release SCL
    while (SCL == 0)
        ;        // Synchronize clock
    i2c_delay(); // Force 5µs-delay
    SCL = 0;     // Force a clock cycle

    EA = 1; // Enable all interrupts

    return;
}

void i2c_nack(void)
{

    EA = 0; // Disable all interrupts for a accurate timing

    // Generate NACK
    SDA = 1; // Release SDA
    SCL = 1; // Release SCL
    while (SCL == 0)
        ;        // Synchronize clock
    i2c_delay(); // Force 5µs-delay
    SCL = 0;     // Force a clock cycle

    EA = 1; // Enable all interrupts

    return;
}

unsigned char i2c_check(void)
{

    EA = 0; // Disable all interrupts for a accurate timing

    SDA = 1; // Release SDA
    SCL = 1; // Release SCL
    while (SCL == 0)
        ;        // Synchronize clock
    i2c_delay(); // Force 5µs-delay
    if (SDA)
    {               // SDA is high
        SCL = 0;    // Force a clock cycle
        return (1); // No acknowledge from Slave
    }
    SCL = 0; // Force a clock cycle

    EA = 1; // Enable all interrupts

    return (0); // Acknowledge from Slave
}

unsigned char i2c_write(unsigned char value)
{
    unsigned char counter = 0; // Bitcounter

    EA = 0; // Disable all interrupts for a accurate timing

    for (counter = 0; counter < 8; counter++)
    {
        SDA = ((value & 0x80) >> 7) & 1; // Set SDA to value of MSB
        SCL = 1;                         // Release SCL
        while (SCL == 0)
            ;        // Synchronize clock
        i2c_delay(); // Force 5µs-delay
        SCL = 0;     // Force a clock cycle
        value <<= 1; // Prepare next bit for transmission
    }

    EA = 1; // Enable all interrupts

    // Generate a 9. clock cycle and check ACK from SLAVE
    // Return the result
    return (i2c_check());
}

unsigned char i2c_read(void)
{
    unsigned char result = 0;  // Returnvalue with read I2C byte
    unsigned char counter = 0; // Bitcounter

    EA = 0; // Disable all interrupts for a accurate timing

    for (counter = 0; counter < 8; counter++)
    {
        SDA = 1; // Release SDA
        SCL = 1; // Release SCL
        while (SCL == 0)
            ;        // Synchronize clock
        i2c_delay(); // Force 5µs-delay

        result <<= 1; // Shift left the result

        if (SDA)
            result |= 0x01; // Set actual SDA state to LSB

        SCL = 0;     // Force a clock cycle
        i2c_delay(); // Force 5µs-delay
    }

    EA = 1; // Enable all interrupts

    // ACK or NACK from MASTER must be generated outside this routine

    return (result);
}

void EEPROM_write(unsigned char adress, unsigned char content)
{

    i2c_start(); // Generate START condition

    // Send SLAVE adress with write request
    if (i2c_write(EEPROM_ID | WRITE))
    {
        printf("1");
        return;
    }

    // Send memory adress
    if (i2c_write(adress))
    {
        printf("E2");
        return;
    }

    // Send content to memory adress
    if (i2c_write(content))
    {
        printf("E3");
        return;
    }

    i2c_stop(); // Generate STOP condition

    // delay(100);    // Secure wait for the EEPROMs write cycle

    return;
}

unsigned char EEPROM_read(unsigned char adress)
{
    unsigned char result = 0;

    i2c_start(); // Generate START condition

    // Send SLAVE adress with dummy write request
    if (i2c_write(EEPROM_ID | WRITE))
    {
        printf("E4");
        return (0);
    }

    // Send memory adress
    if (i2c_write(adress))
    {
        printf("E5");
        return (0);
    }

    i2c_start(); // Generate START condition

    // Send SLAVE adress with read request
    if (i2c_write(EEPROM_ID | READ))
    {
        printf("E6");
        return (0);
    }

    result = i2c_read(); // Read memory content
                         // DonŽt perform a MASTER ACK !
    i2c_stop();          // Generate STOP condition

    return (result);
}

/*
 *                 P1.3 P1.2 P1.1
 *                 KB3  KB2  KB1
 * DRV3 (0x8003) = 1    2    3
 * DRV2 (0x8002) = 4    5    6
 * DRV1 (0x8001) = 7    8    9
 * DRV0 (0x8000) = #    0    *
 */

struct
{
    uint8_t drv_pin;
    uint8_t in_pin;
    char ascii;
}
// NOTE: this is not the original keyboard, it's just a cheap Arduino 3*4 matrix.
const KEYBOARD[] = {
    {3, 3, '1'},
    {3, 2, '2'},
    {3, 1, '3'},

    {2, 3, '4'},
    {2, 2, '5'},
    {2, 1, '6'},
    {1, 3, '7'},
    {1, 2, '8'},
    {1, 1, '9'},

    {0, 3, '*'},
    {0, 2, '0'},
    {0, 1, '#'}};

char kbd_scan(void)
{
    uint8_t i;
    bool rd;

    // start fresh
    _8000[0] = 1;
    _8000[1] = 1;
    _8000[2] = 1;
    _8000[3] = 1;

    for (i = 0; i < (sizeof(KEYBOARD) / sizeof(KEYBOARD[0])); i++)
    {
        // drive the line low
        _8000[KEYBOARD[i].drv_pin] = 0;

        switch (KEYBOARD[i].in_pin)
        {
        case 1:
            rd = P1_1;
            break;
        case 2:
            rd = P1_2;
            break;
        case 3:
            rd = P1_3;
            break;
        default:
            rd = 1;
        }

        // back high again
        _8000[KEYBOARD[i].drv_pin] = 1;

        if (rd == 0)
        {
            return KEYBOARD[i].ascii;
        }
    }

    return '\0';
}

static void serial_init(void)
{
    /* SM: 010, 8 bit, no multiprocessor, enable Rx */
    SCON = 0x50;

    /* Timer1: 8-bit auto reload Timer/Counter, no prescaler */
    TMOD |= 0x20;

    /* Timer1 reload value for 9600 */
    TH1 = (256 - F_CPU / 12 / 32 / 9600.0 + 0.5);

    /* Start Timer1 */
    TCON |= 0x40;

    /* Clear the interrupt flag */
    TI = 1;
}

static void timer0_init(void)
{
    TH0 = T0_RELOAD_H;
    TL0 = T0_RELOAD_L;

    /* Timer0 mode: 16-bit Timer/Counter, no prescaler */
    TMOD |= 0x01;

    /* Enable Timer0 interrupts */
    ET0 = 1;
    /* Enable global interrupts */
    EA = 1;

    /* Start Timer0 */
    TCON |= 0x10;
}

void main(void)
{
    bool backlight = true;

    serial_init();
    timer0_init();
    lcd_init();
    _backlight = 1;

    printf("BOOTED!\n");
    lcd_puts("ABB VFD is screwed!!");

    char kb = 0;
    uint8_t was = 0;
    for (;;)
    {
        if (kbhit())
        {
            // a char should already be available on the serial port
            kb = getchar();
        }
        else
        {
            // scan kbd
            kb = kbd_scan();
            if (kb == '\0')
                continue;
            // wait for release
            while (kbd_scan() != '\0')
                ;
        }

        switch (kb)
        {
        case 'c':
        case 'C':
        {
            printf("set contrast? 0x");
            was = readhex(false);
            printf("%.2x\n", was);
            lcd_set_contrast(was);
            break;
        }
        case 's':
        case 'S':
        {
            lcd_putchar(getchar());
            break;
        }
        case '0':
        {
            was = P1_0;
            P1_0 = was ? 0 : 1;
            printf("0: %d => %d\n", was, P1_0);
            break;
        }
        case '5':
        {
            was = P1_5;
            P1_5 = was ? 0 : 1;
            printf("5: %d => %d\n", was, P1_5);
            break;
        }

        case '6':
        {
            was = P3_2;
            P3_2 = was ? 0 : 1;
            printf("3.2: %d => %d\n", was, P3_2);
            break;
        }
        case '7':
        {
            was = P3_3;
            P3_3 = was ? 0 : 1;
            printf("3.3: %d => %d\n", was, P3_3);
            break;
        }
        case '8':
        {
            was = P3_4;
            P3_4 = was ? 0 : 1;
            printf("3.4: %d => %d\n", was, P3_4);
            break;
        }
        case '9':
        {
            was = P3_5;
            P3_5 = was ? 0 : 1;
            printf("3.5: %d => %d\n", was, P3_5);
            break;
        }

        case '*':
        case 'b':
        case 'B':
        {
            if (backlight)
            {
                backlight = false;
                _backlight = 0;
                printf("backlight: OFF\n");
            }
            else
            {
                backlight = true;
                _backlight = 1;
                printf("backlight: ON\n");
            }
            break;
        }
        case 'd':
        case 'D':
        {
            printf("reading eeprom: \n");
            for (uint16_t addr = 0; addr < 2048; addr++)
            {
                if ((addr % 16) == 0)
                    printf("\n");

                printf("%.2x ", EEPROM_read(addr));
            }
            break;
        }
        case 'r':
        case 'R':
        {
            printf("READ where? 0x");
            uint16_t hex = readhex(true);
            uint8_t read = 0;
            if (hex >= 0xc000)
                read = _c000[hex - 0xc000];
            else if (hex >= 0x8000)
                read = _8000[hex - 0x8000];
            else
                read = _0000[hex];
            printf("%.4x => 0x%.2x\n", hex, read);
            break;
        }

        case 'w':
        case 'W':
        {
            printf("WRITE where? 0x");
            uint16_t hex = readhex(true);
            printf("%.4x => 0x", hex);

            uint8_t wr = readhex(false);
            if (hex >= 0xc000)
                _c000[hex - 0xc000] = wr;
            else if (hex >= 0x8000)
                _8000[hex - 0x8000] = wr;
            else
                _0000[hex] = wr;

            printf("%.2x\n", wr);
            break;
        }
        default:
            printf("'%c' unrecognized\n", kb);
        }
    }
}
