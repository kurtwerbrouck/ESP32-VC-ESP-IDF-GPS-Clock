//18/06/2023
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/portmacro.h"

#include "driver/uart.h"
#include <bits/stdc++.h>
#include <sys/time.h>

#include "esp_log.h"

#include <vector>

using namespace std;

static const char *TAG = "UART-Events";

#define UART_NUM2 1
const uart_port_t uart_num = UART_NUM_2;

#define BUF_SIZE        2048
#define RD_BUF_SIZE     BUF_SIZE
#define PATTERN_CHR_NUM (1) 

extern "C" { void app_main(); }


struct Receiving_Data 
{
    char* Receiving_Data_string;
};

/* trash
struct Data_split
{
    char* time;
    char* lon;
    char* lat;
};
*/

QueueHandle_t uart2_queue                       = NULL;
QueueHandle_t Passing_Received_command_queue    = NULL;

//TaskFunction_t uart2_th;
TaskHandle_t uart2_th;
TaskHandle_t rx_task_th;


void UART_Param () 
{
    uart_config_t uart_config = 
    {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(1, &uart_config));
}

void UART_Pins () 
{
    ESP_ERROR_CHECK(uart_set_pin(1, 4, 5, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));//3.3volt gnd blauw = d4, groen = d5
}

void UART_Driver_Install ()
{
    ESP_ERROR_CHECK(uart_driver_install(1, RD_BUF_SIZE, RD_BUF_SIZE, 10, &uart2_queue, 0));
}  

void uart_event_task(void *args)
{   
    uart_event_t event;
    size_t buffered_size;
    uint8_t* dtmp = (uint8_t*) malloc(RD_BUF_SIZE);
    while(1) 
    {
        //Waiting for UART event.
        if(xQueueReceive(uart2_queue, (void * )&event, (TickType_t)portMAX_DELAY)) {
            
            bzero(dtmp, RD_BUF_SIZE);
            //ESP_LOGI(TAG, "uart[%d] event:", 1);
            switch(event.type) {
                case UART_DATA_BREAK:
                    break;
                case UART_EVENT_MAX:
                    break;
                // case UART_WAKEUP:
                //    break;
                    
                //Event of UART receving data
                //We'd better handler data event fast, there would be much more data events than
                //other types of events. If we take too much time on data event, the queue might
                //be full.
                case UART_DATA:
                    ESP_LOGI(TAG, "[UART DATA]: %d", event.size);
                    uart_read_bytes(1, dtmp, event.size, portMAX_DELAY);
                    ESP_LOGI(TAG, "Data -> %s",(const char*) dtmp);
                    //uart_write_bytes(1, (const char*) dtmp, event.size);
                    //uart_flush_input(1);
                    break;
                //Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    ESP_LOGI(TAG, "hw fifo overflow");
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(1);
                    xQueueReset(uart2_queue);
                    break;
                //Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    ESP_LOGI(TAG, "ring buffer full");
                    // If buffer full happened, you should consider increasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(1);
                    xQueueReset(uart2_queue);
                    break;
                //Event of UART RX break detected
                case UART_BREAK:
                    ESP_LOGI(TAG, "uart rx break");
                    break;
                //Event of UART parity check error
                case UART_PARITY_ERR:
                    ESP_LOGI(TAG, "uart parity error");
                    break;
                //Event of UART frame error
                case UART_FRAME_ERR:
                    ESP_LOGI(TAG, "uart frame error");
                    break;
                //UART_PATTERN_DET
                case UART_PATTERN_DET:
                    uart_get_buffered_data_len(1, &buffered_size);
                    int pos = uart_pattern_pop_pos(1);
                    //ESP_LOGI(TAG, "[UART PATTERN DETECTED] pos: %d, buffered size: %d", pos, buffered_size);
                    if (pos == -1) {
                        // There used to be a UART_PATTERN_DET event, but the pattern position queue is full so that it can not
                        // record the position. We should set a larger queue size.
                        // As an example, we directly flush the rx buffer here.
                        uart_flush_input(1);
                    } else {
                        uart_read_bytes(1, dtmp, pos, 100 / portTICK_PERIOD_MS);
                        uint8_t pat[PATTERN_CHR_NUM + 1];
                        memset(pat, 0x00, sizeof(pat));
                        uart_read_bytes(1, pat, PATTERN_CHR_NUM, 100 / portTICK_PERIOD_MS);
                        //ESP_LOGI(TAG, "read data: %s", dtmp);
                        xQueueSend(Passing_Received_command_queue,&dtmp,( TickType_t ) 0);
                        //ESP_LOGI(TAG, "read pat : %s", pat);
                    }
                    break;
                //Others
                
                //default:
                //    ESP_LOGI(TAG, "uart event type: %d", event.type);
                //    break;
                
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

/*
void rx_task(void *arg)
{
    static const char *RX_TASK_TAG = "RX_TASK";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);
    uint8_t* data = (uint8_t*) malloc(RD_BUF_SIZE+1);
    while (1) 
    {
        const int rxBytes = uart_read_bytes(1, data, RD_BUF_SIZE, 500 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            data[rxBytes] = 0;
            ESP_LOGI(RX_TASK_TAG, "Read %d bytes:\n%s", rxBytes, data);
        }
    }
    free(data);
}   
*/

void Processes_received_GPS_Strings(void* arg)
{   
    char* pch;
    struct Receiving_Data Receiving_DATA;
    vector<char*> split_data;

    struct GPS_Data {
        char* houres = new char[3];
        char* min = new char[3];
        char* sec = new char[3];
        char* day = new char[3];
        char* month = new char[3];
        char* year = new char[3];
        char* lat =  new char[10];
        char* NS =  new char[3];
        char* lon =  new char[13];
        char* EW =  new char[3];
    } GPS_DATA;

    memcpy(GPS_DATA.houres,"xx\0",3);
    memcpy(GPS_DATA.min,"xx\0",3);
    memcpy(GPS_DATA.sec,"xx\0",3);
    memcpy(GPS_DATA.day,"xx\0",3);
    memcpy(GPS_DATA.month,"xx\0",3);
    memcpy(GPS_DATA.year,"xx\0",3);
    memcpy(GPS_DATA.lat,"xxxxxxxxx\0",10);
    memcpy(GPS_DATA.lon,"xxxxxxxxxxxx\0",13);

    for(;;)
    {
       xQueueReceive(Passing_Received_command_queue,&Receiving_DATA,portMAX_DELAY);

       //ESP_LOGI("Receiving","%s",Receiving_DATA.Receiving_Data_string);
       
       pch = strtok(Receiving_DATA.Receiving_Data_string,"$,");
       while(pch != NULL)
       {
            split_data.push_back(pch);
            //ESP_LOGI("CMD","%s",pch);
            pch = strtok (NULL, ",");
        
        }
       
       if (!strcmp("GPRMC",split_data[0])) 
       {
            //ESP_LOGI("GPRMC","%s",split_data[0]);
            
            if ((split_data.size()>=4) and (split_data.size()<=10))
            {
                if (!strcmp("V",split_data[2]))
                {
                    // ESP_LOGI("GPRMC","Received : %s",split_data[split_data.size()-4]);
                    memcpy(GPS_DATA.houres,split_data[1]+0,2);
                    memcpy(GPS_DATA.min,split_data[1]+2,2);
                    memcpy(GPS_DATA.sec,split_data[1]+4,2);
                    memcpy(GPS_DATA.day,split_data[split_data.size()-2]+0,2);
                    memcpy(GPS_DATA.month,split_data[split_data.size()-2]+2,2);
                    memcpy(GPS_DATA.year,split_data[split_data.size()-2]+4,2);
                    ESP_LOGI("GPRMC V","%s:%s:%s\t%s-%s-%s",GPS_DATA.houres,GPS_DATA.min,GPS_DATA.sec,GPS_DATA.day,GPS_DATA.month,GPS_DATA.year);
                }

                if  (!strcmp("A",split_data[2]))
                {
                    // ESP_LOGI("GPRMC","Received : %s",split_data[split_data.size()-4]);
                    memcpy(GPS_DATA.houres,split_data[1]+0,2);
                    memcpy(GPS_DATA.min,split_data[1]+2,2);
                    memcpy(GPS_DATA.sec,split_data[1]+4,2);
                    memcpy(GPS_DATA.day,split_data[split_data.size()-2]+0,2);
                    memcpy(GPS_DATA.month,split_data[split_data.size()-2]+2,2);
                    memcpy(GPS_DATA.year,split_data[split_data.size()-2]+4,2);
                    memcpy(GPS_DATA.lat,split_data[3],9);
                    memcpy(GPS_DATA.lon,split_data[5],12);
                    ESP_LOGI("GPRMC A","%s:%s:%s\t%s-%s-%s\t%s\t%s",GPS_DATA.houres,GPS_DATA.min,GPS_DATA.sec,GPS_DATA.day,GPS_DATA.month,GPS_DATA.year,GPS_DATA.lat,GPS_DATA.lon);
                }

                struct tm t = {0};  // Initalize to all 0's
                t.tm_year = stoi(GPS_DATA.year,0,10) + 100 ; // This is year-1900, so 112 = 2012
                t.tm_mon  = stoi(GPS_DATA.month,0,10) -1 ;
                t.tm_mday = stoi(GPS_DATA.day,0,10);
                t.tm_hour = stoi(GPS_DATA.houres,0,10);
                t.tm_min  = stoi(GPS_DATA.min,0,10);
                t.tm_sec  = stoi(GPS_DATA.sec,0,10);
                time_t timeSinceEpoch = mktime(&t);

                //ESP_LOGI("Epoch before","%lli",timeSinceEpoch);
                
                struct timeval now = { .tv_sec = timeSinceEpoch };
                settimeofday(&now,NULL);
               
                struct timeval tv_now;
                gettimeofday(&tv_now,NULL);

                //ESP_LOGI("Epoch After ","%lli",tv_now.tv_sec);
            }
        }
        
      //debug array
      
      // for (int i=0;i<= split_data.size()-1;i++)
      // {
      //     ESP_LOGI("CMD","%i \t %s",i,split_data[i]);
      // }
      //
      
       split_data.erase(split_data.begin(), split_data.end());
       split_data.clear();
       split_data.shrink_to_fit();
       //ESP_LOGI("CMD","%i %i",split_data.size(),split_data.capacity());
      
    }

}

void app_main(void)
{
    //struct Receiving_Data Receiving_DATA;
    UART_Param();                                   //ESP_LOGI("CMD","Param");
    UART_Pins();                                    //ESP_LOGI("CMD","Pins");
    UART_Driver_Install();                          //ESP_LOGI("CMD","Install");

    uart_enable_pattern_det_baud_intr(1, 0x0A, PATTERN_CHR_NUM, 9, 0, 0);
    uart_pattern_queue_reset(1, 2048);

    setenv("TZ","CET+1CEST,M3.5.0,M10.5.0/3",1);
    tzset();

    //struct Receiving_Data Receiving_DATA = {"ABCDEFGH"};
    Passing_Received_command_queue = xQueueCreate(20,sizeof(struct Receiving_Data *));

    xTaskCreatePinnedToCore(&uart_event_task, "uart_event_task", 4096, NULL, 12, &uart2_th,0);//& voor uart2_th
    //xTaskCreatePinnedToCore(&rx_task, "rx_task", 2048, NULL, 10, &rx_task_th,1);

    char* GPS_Commands = new char[26];
    memcpy(GPS_Commands,"$PUBX,40,GLL,0,0,0,0*5C\r\n\0",26); uart_write_bytes(1, (const char*)GPS_Commands, strlen(GPS_Commands));
    memcpy(GPS_Commands,"$PUBX,40,GGA,0,0,0,0*5A\r\n\0",26); uart_write_bytes(1, (const char*)GPS_Commands, strlen(GPS_Commands));
    //memcpy(GPS_Commands,"$PUBX,40,RMC,0,0,0,0*47\r\n\0",26); uart_write_bytes(1, (const char*)GPS_Commands, strlen(GPS_Commands));
    memcpy(GPS_Commands,"$PUBX,40,GSV,0,0,0,0*59\r\n\0",26); uart_write_bytes(1, (const char*)GPS_Commands, strlen(GPS_Commands));
    memcpy(GPS_Commands,"$PUBX,40,GSA,0,0,0,0*4E\r\n\0",26); uart_write_bytes(1, (const char*)GPS_Commands, strlen(GPS_Commands));
    memcpy(GPS_Commands,"$PUBX,40,VTG,0,0,0,0*5E\r\n\0",26); uart_write_bytes(1, (const char*)GPS_Commands, strlen(GPS_Commands));

/*
    char* test_str = "$PUBX,40,GLL,0,0,0,0*5C\r\n";  
//    test_str = "$PUBX,40,GLL,0,0,0,0*5C\r\n"; uart_write_bytes(1, (const char*)test_str, strlen(test_str));1703016241
    test_str = "$PUBX,40,GGA,0,0,0,0*5A\r\n"; uart_write_bytes(1, (const char*)test_str, strlen(test_str));
    test_str = "$PUBX,40,RMC,0,0,0,0*47\r\n"; uart_write_bytes(1, (const char*)test_str, strlen(test_str));
    test_str = "$PUBX,40,GSV,0,0,0,0*59\r\n"; uart_write_bytes(1, (const char*)test_str, strlen(test_str));
    test_str = "$PUBX,40,GSA,0,0,0,0*4E\r\n"; uart_write_bytes(1, (const char*)test_str, strlen(test_str));
    test_str = "$PUBX,40,VTG,0,0,0,0*5E\r\n"; uart_write_bytes(1, (const char*)test_str, strlen(test_str));
*/    

    xTaskCreatePinnedToCore(&Processes_received_GPS_Strings,"Processes_received_GPS_Strings",4096,NULL,12,NULL,1);
    
    vTaskDelete(NULL);  
}