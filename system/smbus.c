// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2022 Samuel Demeulemeester
//

#include "display.h"

#include "io.h"
#include "tsc.h"
#include "pci.h"
#include "unistd.h"

#include "jedec_id.h"

#define LINE_SPD        12
#define MAX_SPD_SLOT    8

#define SMBHSTSTS smbusbase
#define SMBHSTCNT smbusbase + 2
#define SMBHSTCMD smbusbase + 3
#define SMBHSTADD smbusbase + 4
#define SMBHSTDAT smbusbase + 5

int smbdev, smbfun;
unsigned short smbusbase;

unsigned char spd_raw[256];

static void ich5_get_smb(void)
{
    unsigned long x;

    x = pci_config_read16(0, smbdev, smbfun, 0x20);
    smbusbase = (unsigned short) x & 0xFFFE;
}

unsigned char ich5_smb_read_byte(unsigned char adr, unsigned char cmd)
{
    uint32_t t = 0;
    
    __outb(0x1f, SMBHSTSTS);			// reset SMBus Controller
    __outb(0xff, SMBHSTDAT);
    while(__inb(SMBHSTSTS) & 0x01);		// wait until ready
    __outb(cmd, SMBHSTCMD);
    __outb((adr << 1) | 0x01, SMBHSTADD);
    __outb(0x48, SMBHSTCNT);  

    while (!(__inb(SMBHSTSTS) & 0x02)) {	// wait til command finished
        usleep(1);
        t++;
        if (t > 10000) break;			// break after 10ms
    }
    return __inb(SMBHSTDAT);
}

static int ich5_read_spd(int dimmadr)
{
    int x;
    spd_raw[0] = ich5_smb_read_byte(0x50 + dimmadr, 0);
    if (spd_raw[0] == 0xff)	return -1;		// no spd here
    for (x = 1; x < 256; x++) {
        spd_raw[x] = ich5_smb_read_byte(0x50 + dimmadr, (unsigned char) x);
    }
    return 0;
}

struct pci_smbus_controller {
    unsigned vendor;
    unsigned device;
    char *name;
    void (*get_adr)(void);
    int (*read_spd)(int dimmadr);
};

static struct pci_smbus_controller smbcontrollers[] =
    {
     // Intel SMBUS (DUMMY)
     {0x8086, 0x9C22, "Intel HSW-ULT",	ich5_get_smb, ich5_read_spd},
     {0x8086, 0x8C22, "Intel HSW", 		ich5_get_smb, ich5_read_spd},
     {0x8086, 0x1E22, "Intel Z77", 		ich5_get_smb, ich5_read_spd},
     {0x8086, 0x1C22, "Intel P67", 		ich5_get_smb, ich5_read_spd},
     {0x8086, 0x3B30, "Intel P55", 		ich5_get_smb, ich5_read_spd},
     {0x8086, 0x3A60, "Intel ICH10B", 	ich5_get_smb, ich5_read_spd},
     {0x8086, 0x3A30, "Intel ICH10R", 	ich5_get_smb, ich5_read_spd},
     {0x8086, 0x2930, "Intel ICH9", 		ich5_get_smb, ich5_read_spd},
     {0x8086, 0x283E, "Intel ICH8", 		ich5_get_smb, ich5_read_spd},
     {0x8086, 0x27DA, "Intel ICH7", 		ich5_get_smb, ich5_read_spd},
     {0x8086, 0x266A, "Intel ICH6", 		ich5_get_smb, ich5_read_spd},
     {0x8086, 0x24D3, "Intel ICH5", 		ich5_get_smb, ich5_read_spd},
     {0x8086, 0x24C3, "Intel ICH4", 		ich5_get_smb, ich5_read_spd},
     {0x8086, 0x34A3, "Intel ICL", 		ich5_get_smb, ich5_read_spd},
     {0x8086, 0xA323, "Intel CNL", 		ich5_get_smb, ich5_read_spd},
     {0x8086, 0xA2A3, "Intel CNL", 		ich5_get_smb, ich5_read_spd},  
     {0x8086, 0x9DA3, "Intel CNL", 		ich5_get_smb, ich5_read_spd},     
     {0x8086, 0x9CA2, "Intel BDW", 		ich5_get_smb, ich5_read_spd},
     {0x8086, 0x25A4, "Intel 6300ESB", ich5_get_smb, ich5_read_spd},
     {0x8086, 0x269B, "Intel ESB2", 		ich5_get_smb, ich5_read_spd},
     {0x8086, 0x5032, "Intel EP80579", ich5_get_smb, ich5_read_spd},

     // AMD SMBUS
     {0, 0, "", NULL, NULL}
};

int find_smb_controller(void)
{
    int i = 0;
    unsigned long valuev, valued;
    
    for (smbdev = 0; smbdev < 32; smbdev++) {
        for (smbfun = 0; smbfun < 8; smbfun++) {
            valuev = pci_config_read16(0, smbdev, smbfun, 0);
            if (valuev != 0xFFFF) {					                              // if there is something look what's it..
                for (i = 0; smbcontrollers[i].vendor > 0; i++) {	        // check if this is a known smbus controller
                    if (valuev == smbcontrollers[i].vendor) {
                        valued = pci_config_read16(0, smbdev, smbfun, 2);	// read the device id
                        if (valued == smbcontrollers[i].device) {
                            return i;
                        }
                    }
                }
            }	
        }
    }
    
    return -1;
}

void parse_spd_ddr4(uint8_t idx)
{
    //prints(LINE_SPD+idx, 0, "DDR4");
}

void parse_spd_ddr3(uint8_t idx)
{
    uint8_t curcol = 0;
    uint32_t module_size;
    uint8_t tck;
    
    uint8_t i,h;
    
    if(spd_raw[2] == 0x0B){

        // First print slot#, module capacity
        curcol = prints(LINE_SPD+idx, curcol, " - Slot   :");
        printu(LINE_SPD+idx, curcol-3, idx+1, 1, false, false);

        // module_size = 1 << (sdram_capacity + 15) << (prim_bus_width + 3) >> (sdram_width + 2) << ranks;
        module_size = 1 << ((spd_raw[4] & 0xF) + 15) << ((spd_raw[8] & 0x7) + 3) >> ((spd_raw[7] & 0x7) + 2) << (spd_raw[7] >> 3);
        curcol = printf(LINE_SPD+idx, curcol+1, "%kB", module_size) + 1;

        // If XMP is supported, check Tck in XMP reg					
        if(spd_raw[176] == 0x0C && spd_raw[177] == 0x4A && spd_raw[12])
        {
            tck = spd_raw[186];
        } else {
            tck = spd_raw[12];
        }

        // Then module jedec speed
        switch(tck)
        {
        default:
            curcol = prints(LINE_SPD+idx, curcol, "DDR3-????");
            break;						
        case 20:
            curcol = prints(LINE_SPD+idx, curcol, "DDR3-800");
            break;
        case 15:
            curcol = prints(LINE_SPD+idx, curcol, "DDR3-1066");
            break;
        case 12:
            curcol = prints(LINE_SPD+idx, curcol, "DDR3-1333");
            break;
        case 10:
            curcol = prints(LINE_SPD+idx, curcol, "DDR3-1600");
            break;
        case 9:
            curcol = prints(LINE_SPD+idx, curcol, "DDR3-1866");
            break;
        case 8:
            curcol = prints(LINE_SPD+idx, curcol, "DDR3-2133");
            break;
        case 7:
            curcol = prints(LINE_SPD+idx, curcol, "DDR3-2400");
            break;
        case 6:
            curcol = prints(LINE_SPD+idx, curcol, "DDR3-2533");
            break;
        case 5:
            curcol = prints(LINE_SPD+idx, curcol, "DDR3-2666");
            break;
        }

        if((spd_raw[8] >> 3) == 1) { curcol = prints(LINE_SPD+idx, curcol+1, "ECC");  }

        // Then print module infos (manufacturer & part number)	
        spd_raw[117] &= 0x0F; // Parity odd or even
        for (i = 0; jep106[i].cont_code < 9; i++) {	
            if (spd_raw[117] == jep106[i].cont_code && spd_raw[118] == jep106[i].hex_byte) {
                // We are here if a Jedec manufacturer is detected
                curcol = prints(LINE_SPD+idx, curcol, " - "); 						
                prints(LINE_SPD+idx, curcol, jep106[i].name);
                
                // Display module serial number
                for (h = 128; h < 146; h++) {	
                    curcol = printc(LINE_SPD+idx, curcol, spd_raw[h]);	
                }			

                // Detect Week and Year of Manufacturing (Think to upgrade after 2030 !!!)
                if(curcol <= 72 && spd_raw[120] > 1 && spd_raw[120] < 30 && spd_raw[121] < 55)
                {
                    curcol = prints(LINE_SPD+idx, curcol+1, "(W");	
                    curcol = printi(LINE_SPD+idx, curcol, spd_raw[121], 2, false, false);
                    curcol = printc(LINE_SPD+idx, curcol, '\'');	
                    curcol = printi(LINE_SPD+idx, curcol, spd_raw[120], 2, false, false);
                    curcol = printc(LINE_SPD+idx, curcol, ')');	
                }															
    									
                // Detect XMP Memory
                if(spd_raw[176] == 0x0C && spd_raw[177] == 0x4A)
                {
                    prints(LINE_SPD+idx, curcol+1, "*XMP*");					
                }
            }
        }	    
    }
}

void print_smbus_startup_info(void) {
    
    int8_t index;
    uint8_t spdidx = 0, spd_line_idx = 0;
    
    index = find_smb_controller();

    if (index == -1) 
    {
        return;
    }

    smbcontrollers[index].get_adr();
   
    for (spdidx = 0; spdidx < MAX_SPD_SLOT; spdidx++) {
        if (smbcontrollers[index].read_spd(spdidx) == 0) {	
            switch(spd_raw[2])
            {
                default:
                    continue;
                case 0x0C: // DDR4
                    parse_spd_ddr4(spd_line_idx++);
                    break;
                case 0x0B: // DDR3
                    parse_spd_ddr3(spd_line_idx++);
                    break;
                case 0x08: // DDR2
                    
                    break;
            }         
        }
    }
    
    if(spdidx) {
        prints(LINE_SPD-2, 0, "Memory SPD Informations");
        prints(LINE_SPD-1, 0, "--------------------------");         
    }
   
}
