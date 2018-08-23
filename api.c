
#include <string.h>

#include "system.h"
#include "api.h"
#include "display.h"
#include "os.h"
#include "ui.h"
#include "i2c.h"
#include "bootloader.h"
#include "fat16.h"
#include "flash.h"



/******************************************************************************
 * Static function prototypes
 ******************************************************************************/

static void _fill_buffer_get_status(uint8_t *outBuffer);
static void _fill_buffer_get_display(uint8_t *outBuffer, uint8_t secondHalf);
static void _fill_buffer_get_bootloader_details(uint8_t *outBuffer);
static void _fill_buffer_get_configuration(uint8_t *outBuffer);

static void _fill_buffer_get_file_details(uint8_t *inBuffer, uint8_t *outBuffer);
static void _fill_buffer_find_file(uint8_t *inBuffer, uint8_t *outBuffer);
static void _fill_buffer_read_file(uint8_t *inBuffer, uint8_t *outBuffer);

static void _parse_command_short(uint8_t cmd);
static uint8_t _parse_command_long(uint8_t *data);

static uint8_t _parse_file_truncate(uint8_t *data);
static uint8_t _parse_file_delete(uint8_t *data);
static uint8_t _parse_file_create(uint8_t *data);
static uint8_t _parse_file_rename(uint8_t *data);
static uint8_t _parse_file_append(uint8_t *data);
static uint8_t _parse_file_modify(uint8_t *data);

static uint8_t _parse_settings_spi_mode(uint8_t *data);
static uint8_t _parse_settings_spi_frequency(uint8_t *data);
static uint8_t _parse_settings_spi_polarity(uint8_t *data);
static uint8_t _parse_settings_i2c_mode(uint8_t *data);
static uint8_t _parse_settings_i2c_frequency(uint8_t *data);
static uint8_t _parse_settings_i2c_slaveModeSlaveAddress(uint8_t *data);
static uint8_t _parse_settings_i2c_masterModeSlaveAddress(uint8_t *data);

/******************************************************************************
 * Public functions implementation
 ******************************************************************************/


void api_prepare(uint8_t *inBuffer, uint8_t *outBuffer)
{
    apiDataRequest_t command = (apiDataRequest_t) inBuffer[0]; 

    if(command>0x7F)
    {
        //Extended data request, may be followed by parameters (no commands allowed to follow)
        
        //Only allowed if flash is not busy, return normal status if flash is busy
        if(flash_is_busy())
        {
            _fill_buffer_get_status(outBuffer);
            return;
        }
        
        
        switch(command)
        {
            case DATAREQUEST_GET_FILE_DETAILS:
                //Get file details
                _fill_buffer_get_file_details(inBuffer, outBuffer);
                break;
                
            case DATAREQUEST_FIND_FILE:
                //Find file
                _fill_buffer_find_file(inBuffer, outBuffer);
                break;
                
            case DATAREQUEST_READ_FILE:
                //Read file
                _fill_buffer_read_file(inBuffer, outBuffer);
                break;
        }
    }
    else
    {
        //Normal data request, need no parameters, may be followed by commands
        switch(command)				
        {
            case DATAREQUEST_GET_STATUS:
                //Call function to fill the buffer with general information
                _fill_buffer_get_status(outBuffer);
                break;

            case DATAREQUEST_GET_DISPLAY_1:
                //Call function to fill the buffer with general information
                _fill_buffer_get_display(outBuffer, 0);
                break;

            case DATAREQUEST_GET_DISPLAY_2:
                //Call function to fill the buffer with general information
                _fill_buffer_get_display(outBuffer, 1);
                break;
                
            case DATAREQUEST_GET_BOOTLOADER_DETAILS:
                //Call function to fill the buffer with bootloader details
                _fill_buffer_get_bootloader_details(outBuffer);
                break;
            
            case DATAREQUEST_GET_CONFIGURATION:
                //Call function to fill the buffer with configuration details
                _fill_buffer_get_configuration(outBuffer);
                break;
                
            case DATAREQUEST_GET_ECHO:
                //Copy received data to outBuffer
                memcpy(outBuffer, inBuffer, 64);
                break;
        }
    }
}

void api_parse(uint8_t *inBuffer, uint8_t receivedDataLength)
{
    //Check if the host expects us to do anything else

    uint8_t idx;
    
    if(inBuffer[0]>0x7F)
    {
        //Extended data request. May not be followed by commands.
        //Nothing for us to do here
        return;
    }
    
    idx = 1;
    while(idx<receivedDataLength)
    {
        //Check if there is anything more to parse
        if(inBuffer[idx]==COMMAND_STOP_PARSING)
        {
            return;
        }
        
        switch(inBuffer[idx] & 0xF0)
        {
            case 0x20:
                _parse_command_short(inBuffer[idx]);
                ++idx;
                break;
                
            case 0x30:
                _parse_command_short(inBuffer[idx]);
                ++idx;
                break;
                
            case 0x50:
                idx += _parse_command_long(inBuffer[idx]);
                break;
                
            default:
                //We should never end up here
                //If we still do, stop parsing this buffer
                return;
        }
    }
}


/******************************************************************************
 * Static functions implementation
 ******************************************************************************/

//Fill buffer with general status information
static void _fill_buffer_get_status(uint8_t *outBuffer)
{
    //Echo back to the host PC the command we are fulfilling in the first uint8_t
    outBuffer[0] = DATAREQUEST_GET_STATUS;
    
    //Bootloader signature
    outBuffer[1] = BOOTLOADER_SIGNATURE >> 8; //MSB
    outBuffer[2] = (uint8_t) BOOTLOADER_SIGNATURE; //LSB
    
    
    //Flash busy or not
    outBuffer[3] = (uint8_t) flash_is_busy();
    
    //Firmware version
    outBuffer[4] = FIRMWARE_VERSION_MAJOR;
    outBuffer[5] = FIRMWARE_VERSION_MINOR;
    outBuffer[6] = FIRMWARE_VERSION_FIX;
    
    //Display status, display off, startup etc
    outBuffer[7] = ui_get_status();
    
    //Entire os struct
    outBuffer[8] = os.encoderCount;
    outBuffer[9] = os.buttonCount;
    outBuffer[10] = os.timeSlot;
    outBuffer[11] = os.done;
    outBuffer[12] = os.bootloader_mode;
    outBuffer[13] = os.display_mode;
}

//Fill buffer with display content
static void _fill_buffer_get_display(uint8_t *outBuffer, uint8_t secondHalf)
{
    uint8_t cntr;
    uint8_t line;
    uint8_t start_line;
    uint8_t position;
    
    //Echo back to the host PC the command we are fulfilling in the first uint8_t
    if(secondHalf)
    {
        outBuffer[0] = DATAREQUEST_GET_DISPLAY_2;
    }
    else
    {
        outBuffer[0] = DATAREQUEST_GET_DISPLAY_1;
    }
   
    //Bootloader signature
    outBuffer[1] = BOOTLOADER_SIGNATURE >> 8; //MSB
    outBuffer[2] = (uint8_t) BOOTLOADER_SIGNATURE; //LSB
   
    //Get display data
    cntr = 3;
    if(secondHalf)
    {
        start_line = 2;
    }
    else
    {
        start_line = 0;
    }
    for(line=start_line; line<start_line+2; ++line)
    {
        for(position=0; position<20; ++position)
        {
            outBuffer[cntr] = display_get_character(line, position);
            ++cntr;
        }
    }
}

static void _fill_buffer_get_bootloader_details(uint8_t *outBuffer)
{
    uint8_t cntr;
    uint8_t data_length;
    uint16_t buffer_small;
    uint32_t buffer_large;
    
    //Echo back to the host PC the command we are fulfilling in the first uint8_t
    outBuffer[0] = DATAREQUEST_GET_BOOTLOADER_DETAILS;
    
    //Bootloader signature
    outBuffer[1] = BOOTLOADER_SIGNATURE >> 8; //MSB
    outBuffer[2] = (uint8_t) BOOTLOADER_SIGNATURE; //LSB
   
    //Bootloader information (high level)
    buffer_large = bootloader_get_file_size();
    outBuffer[3] = (uint8_t) buffer_large; //LSB
    outBuffer[4] = (uint8_t) (buffer_large>>8);
    outBuffer[5] = (uint8_t) (buffer_large>>16);
    outBuffer[6] = (uint8_t) (buffer_large>>32); //MSB
    
    buffer_small = bootloader_get_entries();
    outBuffer[7] = (uint8_t) buffer_small; //LSB
    outBuffer[8] = (uint8_t) (buffer_small>>8); //LSB
    
    buffer_small = bootloader_get_total_entries();
    outBuffer[9] = (uint8_t) buffer_small; //LSB
    outBuffer[10] = (uint8_t) (buffer_small>>8); //LSB
    
    outBuffer[11] = (uint8_t) bootloader_get_error();
    
    buffer_small = bootloader_get_flashPagesWritten();
    outBuffer[12] = (uint8_t) buffer_small; //LSB
    outBuffer[13] = (uint8_t) (buffer_small>>8); //LSB
    
    //Bootloader information (last record)
    buffer_small = bootloader_get_rec_dataLength();
    outBuffer[14] = (uint8_t) buffer_small; //LSB
    outBuffer[15] = (uint8_t) (buffer_small>>8); //LSB
    
    buffer_small = bootloader_get_rec_address();
    outBuffer[16] = (uint8_t) buffer_small; //LSB
    outBuffer[17] = (uint8_t) (buffer_small>>8); //LSB
    
    outBuffer[18] = (uint8_t) bootloader_get_rec_recordType();
    outBuffer[19] = bootloader_get_rec_checksum();
    outBuffer[20] = bootloader_get_rec_checksumCheck();

    data_length = (uint8_t) bootloader_get_rec_dataLength();
    if(data_length>43)
    {
        //More will not fit into our 64byte buffer
        data_length = 43;
    }
    for(cntr=0; cntr<data_length; ++cntr)
    {
        outBuffer[21+cntr] = bootloader_get_rec_data(cntr);
    }
}

static void _fill_buffer_get_configuration(uint8_t *outBuffer)
{
    //Echo back to the host PC the command we are fulfilling in the first uint8_t
    outBuffer[0] = DATAREQUEST_GET_STATUS;
    
    //Bootloader signature
    outBuffer[1] = BOOTLOADER_SIGNATURE >> 8; //MSB
    outBuffer[2] = (uint8_t) BOOTLOADER_SIGNATURE; //LSB
   
    //SPI settings
    outBuffer[3] = communicationSettings.spiMode;
    outBuffer[4] = communicationSettings.spiFrequency;
    outBuffer[5] = communicationSettings.spiPolarity;
    
    //I2C settings
    outBuffer[6] = communicationSettings.i2cMode;
    outBuffer[7] = communicationSettings.i2cFrequency;
    outBuffer[8] = communicationSettings.i2cSlaveModeSlaveAddress;
    outBuffer[9] = communicationSettings.i2cMasterModeSlaveAddress;
}

static void _fill_buffer_get_file_details(uint8_t *inBuffer, uint8_t *outBuffer)
{
    uint8_t file_number = inBuffer[2];
    
    //Echo command
    outBuffer[0] = DATAREQUEST_GET_FILE_DETAILS;
   
    //Bootloader signature
    outBuffer[1] = BOOTLOADER_SIGNATURE >> 8; //MSB
    outBuffer[2] = (uint8_t) BOOTLOADER_SIGNATURE; //LSB
    
    //Echo file number
    outBuffer[3] = file_number;
    
    //Return desired data, i.e. entire 32-bit file information struct
    outBuffer[4] = fat_get_file_information(file_number, (rootEntry_t*) &outBuffer[5]);
}

static void _fill_buffer_find_file(uint8_t *inBuffer, uint8_t *outBuffer)
{
    uint8_t cntr;
    
    //Echo command
    outBuffer[0] = DATAREQUEST_FIND_FILE;
   
    //Bootloader signature
    outBuffer[1] = BOOTLOADER_SIGNATURE >> 8; //MSB
    outBuffer[2] = (uint8_t) BOOTLOADER_SIGNATURE; //LSB
    
    //Find and return file number
    outBuffer[3] = fat_find_file(&inBuffer[1], &inBuffer[9]);
    
    //Echo file name and extention
    for(cntr=0; cntr<11; ++cntr)
    {
        outBuffer[cntr+12] = inBuffer[cntr+1];
    }
}

static void _fill_buffer_read_file(uint8_t *inBuffer, uint8_t *outBuffer)
{
    uint32_t start;
    uint32_t file_size;
    uint32_t data_length;
    
    //Echo command
    outBuffer[0] = DATAREQUEST_FIND_FILE;
   
    //Bootloader signature
    outBuffer[1] = BOOTLOADER_SIGNATURE >> 8; //MSB
    outBuffer[2] = (uint8_t) BOOTLOADER_SIGNATURE; //LSB
    
    //Echo file number
    outBuffer[3] = inBuffer[1];
    
    //Echo start
    outBuffer[4] = inBuffer[1];
    outBuffer[5] = inBuffer[2];
    outBuffer[6] = inBuffer[3];
    outBuffer[7] = inBuffer[4];
    
    //Calculate start
    start = inBuffer[4];
    start <<= 8;
    start |= inBuffer[3];
    start <<= 8;
    start |= inBuffer[2];
    start <<= 8;
    start |= inBuffer[1];
    
    //Get file size and calculate number of bytes to get
    file_size = fat_get_file_size(inBuffer[1]);
    data_length = file_size - start;
    if(data_length>54)
    {
        //More will not fit into our 64 byte buffer
        data_length = 54;
    }
    
    //Echo data length
    outBuffer[8] = (uint8_t) data_length;
    
    //Read data from file
    outBuffer[9] = fat_read_from_file(inBuffer[1], start, data_length, &outBuffer[10]);
}


static void _parse_command_short(uint8_t cmd)
{
    switch(cmd)
    {
        case COMMAND_REBOT:
            jump_to_zero();
            break;
            
        case COMMAND_REBOT_BOOTLOADER_MODE:
            i2c_eeprom_writeByte(EEPROM_BOOTLOADER_BYTE_ADDRESS, BOOTLOADER_BYTE_FORCE_BOOTLOADER_MODE);
            system_delay_ms(10); //ensure data has been written before rebooting
            jump_to_zero();
            break;
                
        case COMMAND_REBOT_NORMAL_MODE:
            i2c_eeprom_writeByte(EEPROM_BOOTLOADER_BYTE_ADDRESS, BOOTLOADER_BYTE_FORCE_NORMAL_MODE);
            system_delay_ms(10); //ensure data has been written before rebooting
            jump_to_zero();
            break;
            
        case COMMAND_JUMP_TO_MAIN_PROGRAM:
            jump_to_main_program();
            break;
                
        case COMMAND_ENCODER_CCW:
            --os.encoderCount;
            break;
            
        case COMMAND_ENCODER_CW:
            ++os.encoderCount;
            break;
            
        case COMMAND_ENCODER_PUSH:
            ++os.buttonCount;
            break;
    }
}

static uint8_t _parse_command_long(uint8_t *data)
{
    uint8_t length = 65;
    
    switch(data[0])
    {
        case COMMAND_FILE_TRUNCATE:
            length = _parse_file_truncate(data);
            break;
            
        case COMMAND_FILE_DELETE:
            length = _parse_file_delete(data);
            break;
            
        case COMMAND_FILE_CREATE:
            length = _parse_file_create(data);
            break;
            
        case COMMAND_FILE_RENAME:
            length = _parse_file_rename(data);
            break;
            
        case COMMAND_FILE_APPEND:
            length = _parse_file_append(data);
            break;
            
        case COMMAND_FILE_MODIFY:
            length = _parse_file_modify(data);
            break;
    }    
    
    return length;
}

static uint8_t _parse_file_truncate(uint8_t *data)
{
    //0x50: Truncate file. Parameters: uint8_t FileNumber, uint32_t newFileSize, 0x4CEA
    //Not yet implemented in fat16.h
    return 65;
}

static uint8_t _parse_file_delete(uint8_t *data)
{
    //0x51: Delete file. Parameters: uint8_t FileNumber, 0x66A0
    if((data[0]!=COMMAND_FILE_DELETE) || (data[2]!=0x66) || (data[3]!=0xA0))
    {
        return 4;
    }
    
    //Delete file
    fat_delete_file(data[1]);
    return 4;
}

static uint8_t _parse_file_create(uint8_t *data)
{
    //0x52: Create file. Parameters: char[8] FileName, char[3] FileExtention, 0xBD4F
    if((data[0]!=COMMAND_FILE_CREATE) || (data[12]!=0xBD) || (data[13]!=0x4F))
    {
        return 14;
    }
    
    //Create file
    fat_create_file(&data[1], &data[9], 0);
    return 14;
}

static uint8_t _parse_file_rename(uint8_t *data)
{
    //0x53: Rename file. Parameters: uint8_t FileNumber, char[8] NewFileName, char[3] NewFileExtention, 0x7E18
    if((data[0]!=COMMAND_FILE_RENAME) || (data[13]!=0x7E) || (data[14]!=0x18))
    {
        return 15;
    }
    
    //Rename file
    fat_rename_file(data[1], &data[2], &data[10]);
    return 15;
}

static uint8_t _parse_file_append(uint8_t *data)
{
    //0x54: Append to file. Parameters: uint8_t FileNumber, uint8_t NumberOfBytes, 0xFE4B, DATA
    if((data[0]!=COMMAND_FILE_APPEND) || (data[3]!=0xFE) || (data[4]!=0x4B))
    {
        return 15;
    }
    
    //Append to file
    fat_append_to_file(data[1], (uint16_t) data[2], &data[5]);
    return data[2] + 5;
}

static uint8_t _parse_file_modify(uint8_t *data)
{
    //0x55: Modify file. Parameters: uint8_t FileNumber, uint32_t StartByte, uint8_t NumerOfBytes, 0x0F9B, DATA
    //Not yet implemented in fat16.h
    return 65;
}

static uint8_t _parse_settings_spi_mode(uint8_t *data)
{
    //0x60: Change SPI mode. Parameters: uint8_t NewMode, 0x88E2
    return 4;
}

static uint8_t _parse_settings_spi_frequency(uint8_t *data)
{
    //0x61: Change SPI frequency. Parameters: uint8_t NewFrequency, 0xAEA8
    return 4;
}

static uint8_t _parse_settings_spi_polarity(uint8_t *data)
{
    //0x62: Change SPI polarity. Parameters: uint8_t NewPolarity, 0x0DBB
    return 4;
}

static uint8_t _parse_settings_i2c_mode(uint8_t *data)
{
    //0x63: Change I2C mode. Parameters: uint8_t NewMode, 0xB6B9
    return 4;
}

static uint8_t _parse_settings_i2c_frequency(uint8_t *data)
{
    //0x64: Change I2C frequency. Parameters: uint8_t NewFrequency, 0x4E03
    return 4;
}

static uint8_t _parse_settings_i2c_slaveModeSlaveAddress(uint8_t *data)
{
    //0x65: Change I2C slave mode slave address. Parameters: uint8_t NewAddress, 0x88E2
    return 4;
}

static uint8_t _parse_settings_i2c_masterModeSlaveAddress(uint8_t *data)
{
    //0x66: Change I2C master mode slave address. Parameters: uint8_t NewAddress, 0x540D
    return 4;
}

