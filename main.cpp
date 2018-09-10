#include "mbed.h"
#include <ctype.h>
struct th_ctx {
    const char *name;
    spi_t *itf;
};

Semaphore join(0, 2);
Timer t;

spi_t slave = {0};
spi_t master = {0};

const uint8_t tx_master[] = "0123456789abcdefghijklmnopqrstuvwxyz=+/-*!?";
const uint8_t tx_slave[]  = "zyxwvutsrqponmlkjihgfedcba9876543210=+/-*!?";

uint8_t rx_slave[sizeof(tx_master)+1] = {0};
uint8_t rx_master[sizeof(tx_slave)+1] = {0};

// gpio MBED_CONF_APP_MASTER_SSEL
DigitalOut ssel(MBED_CONF_APP_MASTER_SSEL, 1);

uint32_t fill = (uint32_t)'|';

void fn_slave() {
#if 0
    for (uint32_t i = 0; i < sizeof(tx_slave); i++) {
        spi_transfer(&slave, &tx_slave[i], 1, &rx_slave[i], 1, &fill);
    }
#else 
    spi_transfer(&slave, tx_slave, sizeof(tx_slave), rx_slave, sizeof(tx_master), &fill);
#endif
    join.release();
}

void fn_master() {
    ssel = 0;
    for (uint32_t i = 0; i < sizeof(tx_master); i++) {
        spi_transfer(&master, &tx_master[i], 1, &rx_master[i], 1, &fill);
        Thread::yield(); // release core time to let slave thread handle the reception & next transmition
    }
    ssel = 1;
    join.release();
}

void test1() {
    printf("Testing the blocking API.\n");

    ssel = 1; // make sure ssel is cleared
    spi_init(&slave, true, MBED_CONF_APP_SLAVE_MOSI, MBED_CONF_APP_SLAVE_MISO, MBED_CONF_APP_SLAVE_MCLK, MBED_CONF_APP_SLAVE_SSEL); // enable slave
    spi_init(&master, false, MBED_CONF_APP_MASTER_MOSI, MBED_CONF_APP_MASTER_MISO, MBED_CONF_APP_MASTER_MCLK, NC); // enable master
    
    spi_format(&slave, 8, SPI_MODE_IDLE_LOW_SAMPLE_FIRST_EDGE, SPI_BIT_ORDERING_MSB_FIRST); // slave
    spi_format(&master, 8, SPI_MODE_IDLE_LOW_SAMPLE_FIRST_EDGE, SPI_BIT_ORDERING_MSB_FIRST); // master
    uint32_t freq = 20000;
    uint32_t actual_slave_freq = spi_frequency(&master, freq); // master
    uint32_t actual_master_freq = spi_frequency(&master, freq); // master
    MBED_ASSERT(actual_slave_freq == actual_master_freq);

    printf("freq=%lu, actual freq=%lu\n", freq, actual_master_freq);
    t.reset();
    t.start();

    memset(rx_slave, 0, sizeof(rx_slave));
    memset(rx_master, 0, sizeof(rx_master));

    // thread 1 listen
    Thread tslave;
    tslave.start(callback(fn_slave));
    // thread 2 receive
    Thread tmaster;
    tmaster.start(callback(fn_master));
    
    // wait on semphr
    uint32_t i = 0;
    uint64_t cnt = 0;
    while ((i < 2) && (t.read_ms() < 10000)) {
        if (join.wait(0) != 0) {
            i += 1;
        }
        cnt += 1;
    }
   
    t.stop();

    tslave.terminate();
    tmaster.terminate();

    printf("Ran in %d (cnt = %llu) : %f\n", t.read_ms(), cnt, (float)cnt / (float)t.read_ms());
    printf("s sent  : %s\n", tx_slave);
    printf("m recved: %s\n", rx_master); // should used a "received" value here
    printf("m sent  : %s\n", tx_master);
    printf("s recved: %s\n", rx_slave); // should used a "received" value here
    printf("---\n");

    ssel = 1; // make sure ssel is cleared
    spi_free(&master); // master
    spi_free(&slave); // slave
}

void slave_handler(spi_t *obj, void *ctx, spi_async_event_t *event) {
    join.release();
}
void master_handler(spi_t *obj, void *ctx, spi_async_event_t *event) {
    join.release();
}

void test2() {
    printf("Test the async API.\n");

    ssel = 1; // make sure ssel is cleared
    spi_init(&slave, true, MBED_CONF_APP_SLAVE_MOSI, MBED_CONF_APP_SLAVE_MISO, MBED_CONF_APP_SLAVE_MCLK, MBED_CONF_APP_SLAVE_SSEL); // enable slave
    spi_init(&master, false, MBED_CONF_APP_MASTER_MOSI, MBED_CONF_APP_MASTER_MISO, MBED_CONF_APP_MASTER_MCLK, NC); // enable master
    
    spi_format(&slave, 8, SPI_MODE_IDLE_LOW_SAMPLE_FIRST_EDGE, SPI_BIT_ORDERING_MSB_FIRST); // slave
    spi_format(&master, 8, SPI_MODE_IDLE_LOW_SAMPLE_FIRST_EDGE, SPI_BIT_ORDERING_MSB_FIRST); // master
    uint32_t freq = 20000;
    uint32_t actual_slave_freq = spi_frequency(&master, freq); // master
    uint32_t actual_master_freq = spi_frequency(&master, freq); // master
    MBED_ASSERT(actual_slave_freq == actual_master_freq);

    printf("freq=%lu, actual freq=%lu\n", freq, actual_master_freq);
    t.reset();
#if DEVICE_SPI_ASYNCH
    t.start();

    memset(rx_slave, 0, sizeof(rx_slave));
    memset(rx_master, 0, sizeof(rx_master));

    spi_transfer_async(&slave, tx_slave, sizeof(tx_slave), rx_slave, sizeof(rx_slave), &fill,
            slave_handler, NULL, DMA_USAGE_OPPORTUNISTIC); // prepare listen
    
    ssel = 0;
    spi_transfer_async(&master, tx_master, sizeof(tx_master), rx_master, sizeof(rx_master), &fill,
            master_handler, NULL, DMA_USAGE_OPPORTUNISTIC); // prepare send

    // wait on semphr
    uint32_t i = 0;
    uint64_t cnt = 0;
    while ((i < 2) && (t.read_ms() < 10000)) {
        if (join.wait(0) != 0) {
            i += 1;
        }
        cnt += 1;
    }
    ssel = 1;

    t.stop();
    printf("Ran in %d (cnt = %llu) : %f\n", t.read_ms(), cnt, (float)cnt / (float)t.read_ms());
    printf("s sent  : %s\n", tx_slave);
    printf("m recved: %s\n", rx_master); // should used a "received" value here
    printf("m sent  : %s\n", tx_master);
    printf("s recved: %s\n", rx_slave); // should used a "received" value here
#else
    printf("not supported.\n");
#endif
    printf("---\n");

    ssel = 1; // make sure ssel is cleared
    spi_free(&master); // master
    spi_free(&slave); // slave
}

int main() {
    test1();
    test2();
    printf("All tests done !\n");
    while (true);
}
