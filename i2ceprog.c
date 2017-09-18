#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <stdint.h>

/* POSIX headers */
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <dev/i2c/i2c_io.h>

int write_eeprom(int fd, uint16_t addr, uint8_t val);
int write_eeprom_page(int fd, uint16_t addr, uint8_t num, uint8_t * vals);
int read_eeprom_seq(int fd, uint8_t num, uint8_t * vals);
int read_eeprom_rand(int fd, uint16_t addr, uint8_t * val);

/* Helper functions */
int get_file_block(FILE * fp, uint8_t * out_array, int size); 
void print_buf(uint8_t * arr, int size);

int main(int argc, char * argv[])
{
   int i2cfd, i, rc = EXIT_FAILURE;
   uint8_t dummy_byte; /* Used to overflow EEPROM internal addr. */
   unsigned char read_val;
   i2c_ioctl_exec_t iie;
   FILE * infile; 

   if(argc < 2)
   {
      printf("File name to write to EEPROM missing!\n");
      return EXIT_FAILURE;
   }

   infile = fopen(argv[1], "rb");
   if(infile == NULL)
   {
      printf("File open for %s failed! (%s)\n", argv[1], strerror(errno));
      return EXIT_FAILURE;
   }

   /* On the RPi, at least later Model A/Bs, GPIO exists on /dev/iic1 */
   i2cfd = open("/dev/iic1", O_RDWR);
   if(i2cfd == -1)
   {
      printf("File open for I2C bus failed! (%s)\n", strerror(errno));
      fclose(infile);
      return EXIT_FAILURE;
   }
   
   
   for(i = 0; i < 512; i += 16)
   {
      int bytes_read, short_read = 0;
      uint8_t page_array[16];
 
      int filerc = get_file_block(infile, page_array, 16);
         
      if(filerc < 0) 
      {
         printf("Input file read failed! (%s) \n", strerror(errno));
         goto failure_cleanup;
      } 

      if(short_read == 0 && filerc >= 1)
      {
         printf("Warning! Short file read! Padding with 0xFF...\n");
         short_read = 1;
      }

      printf("Page: %X, byte_no: %X: ", ((i & 0x0100) >> 8), (i & 0x00FF));
      print_buf(page_array, 16);


     /* For a fresh EEPROM, 0xFF should be the default value.
      Let's rewrite it! */
      if(write_eeprom_page(i2cfd, i, 16, page_array))
      {
         printf("I2C write failed! (%s) \n", strerror(errno));
         goto failure_cleanup;
      }

      usleep(5000); /* 10 times the maximum time for internal writes to
                      register according to AT24HC04B datasheet (5ms) */
   }

   printf("Verifying EEPROM write...\n");
   rewind(infile);
   /* Dummy read to overflow internal address counter to 0. */
   (void) read_eeprom_rand(i2cfd, 511, &dummy_byte);

   for(i = 0; i < 512; i += 16)
   {
      int short_read;
      uint8_t eeprom_buf[16];
      uint8_t file_buf[16];
 
      int filerc = get_file_block(infile, file_buf, 16);
      if(read_eeprom_seq(i2cfd, 16, eeprom_buf))
      {
         printf("I2C read failed! (%s)\n", strerror(errno));
         break;
      }
      
      printf("EEPROM buf: "); print_buf(eeprom_buf, 16);
      printf("File buf: "); print_buf(file_buf, 16);
        
      if(filerc < 0)
      {
         printf("Input file read failed! (%s) \n", strerror(errno));
         break;
      } 
      else
      {
         if(memcmp(eeprom_buf, file_buf, 16))
         {
            printf("EEPROM to file compare failed (buffers do not match)!\n");
            break;
         }
      }
   }

   if(i == 512)
   {
      printf("\nEEPROM programmed successfully.\n"); 
      rc = EXIT_SUCCESS;
   }
   else
   {
      printf("\nEEPROM program verify failed. Check connections.\n");
      rc = EXIT_FAILURE;
   } 

failure_cleanup:
   close(i2cfd);
   fclose(infile);
   return rc;

}

int write_eeprom(int fd, uint16_t addr, uint8_t val)
{
   i2c_ioctl_exec_t iie;
   uint8_t page, byte_no;

   /* Page refers to both 16-byte and 256-byte boundaries. */
   page = ((addr & 0x0100) >> 8);
   byte_no = (addr & 0x00FF);


   iie.iie_op = I2C_OP_WRITE_WITH_STOP;
   iie.iie_addr = 0x50 | page; /* Default for ATMEL EEPROMs, 
               A0 is used for addr of 256b page .*/

   iie.iie_cmd = &byte_no; /* Send the write byte address first. */
   iie.iie_cmdlen = 1;
   iie.iie_buf = &val;
   iie.iie_buflen = 1;

   return ioctl(fd, I2C_IOCTL_EXEC, &iie);
}


/* Returns -1 on IOCTL error, -2 on bad input. */
int write_eeprom_page(int fd, uint16_t addr, uint8_t num, uint8_t * vals)
{
   i2c_ioctl_exec_t iie;
   uint8_t page, byte_no;

   /* For simplicity, if doing a page-write, start on page boundary. */
   if((num > 16) || ((addr % 16) != 0))
   {
      return -2;
   } 


   page = ((addr & 0x0100) >> 8);
   byte_no = (addr & 0x00FF);

   iie.iie_op = I2C_OP_WRITE_WITH_STOP;
   iie.iie_addr = 0x50 | page; /* Default for ATMEL EEPROMs, 
               A0 is used for addr of 256b page .*/

   iie.iie_cmd = &byte_no; /* Send the write byte address first. */
   iie.iie_cmdlen = 1;
   iie.iie_buf = vals;
   iie.iie_buflen = num;

   return ioctl(fd, I2C_IOCTL_EXEC, &iie);
}

/* Will read from the last-previously accessed location, plus one.
Exercise for the user of this code:
Combine with read_eeprom_rand to create a bulk read from address.
(i.e. read_eeprom_bulk) */
int read_eeprom_seq(int fd, uint8_t num, uint8_t * vals)
{
   i2c_ioctl_exec_t iie;

   iie.iie_op = I2C_OP_READ_WITH_STOP;
   iie.iie_addr = 0x50; /* EEPROM already set up- no page needed. */
   iie.iie_cmd = NULL; /* EEPROM already set up- no cmd/address to send.  */
   iie.iie_cmdlen = 0;
   iie.iie_buf = vals;
   iie.iie_buflen = num;
   
   return ioctl(fd, I2C_IOCTL_EXEC, &iie);
}

int read_eeprom_rand(int fd, uint16_t addr, uint8_t * val)
{
   i2c_ioctl_exec_t iie;
   uint8_t page, byte_no;

   page = ((addr & 0x0100) >> 8);
   byte_no = (addr & 0x00FF);

   iie.iie_op = I2C_OP_READ_WITH_STOP;
   iie.iie_addr = 0x50 | page; /* Default for ATMEL EEPROMs, 
               A0 is used for addr of 256b page .*/
   iie.iie_cmd = &byte_no; /* To do a random read, we need to send the
               byte address first. */
   iie.iie_cmdlen = 1;
   iie.iie_buf = val;
   iie.iie_buflen = 1;

   return ioctl(fd, I2C_IOCTL_EXEC, &iie);
}


/* Return:
0- No error
1- Short read, padded block
-1- File read error
Attempts to read part of a file. If EOF is reached, pad with 0xFF until end. */
int get_file_block(FILE * fp, uint8_t * out_array, int size)
{
   int bytes_read, rc = 0;

   bytes_read = fread(out_array, sizeof(uint8_t) /* 1 */, size, fp);
   
   if(bytes_read < size)
   {
      if(feof(fp))
      {
         memset(out_array + bytes_read, 0xFF, size - bytes_read);
         rc = 1;
      }
      else
      {
         rc = -1;
      }
   }
   
   return rc;
}

/* Helper function for printing out buffers! */
void print_buf(uint8_t * arr, int size)
{
   int i;

   for(i = 0; i < size - 1; i++)
   {
      printf("%X ", arr[i]);
   }
   
   printf("%X\n", arr[i]);  
}

