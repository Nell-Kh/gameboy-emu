#include "core/serial.h"

#include <gtest/gtest.h>

namespace {

using core::Serial;

TEST(Serial, DataRegisterReadsBackWhatWasWritten) {
    Serial serial;
    serial.write(Serial::kData, 0x42);
    EXPECT_EQ(serial.read(Serial::kData), 0x42);
}

TEST(Serial, UnusedControlBitsReadAsOne) {
    Serial serial;
    EXPECT_EQ(serial.read(Serial::kControl), 0x7E);
    serial.write(Serial::kControl, 0x01);
    EXPECT_EQ(serial.read(Serial::kControl), 0x7F);
}

TEST(Serial, StartingATransferSendsTheDataByte) {
    Serial serial;
    serial.write(Serial::kData, 'O');
    serial.write(Serial::kControl, 0x81);
    serial.write(Serial::kData, 'K');
    serial.write(Serial::kControl, 0x81);
    EXPECT_EQ(serial.output(), "OK");
}

TEST(Serial, TransferTakesEightBitTimes) {
    Serial serial;
    serial.write(Serial::kData, 0x12);
    serial.write(Serial::kControl, 0x81);

    EXPECT_FALSE(serial.tick(Serial::kTicksPerTransfer - 1));
    EXPECT_EQ(serial.read(Serial::kControl), 0xFF);
    EXPECT_TRUE(serial.tick(1));
}

TEST(Serial, FinishedTransferClearsTheStartBitAndShiftsInFF) {
    Serial serial;
    serial.write(Serial::kData, 0x12);
    serial.write(Serial::kControl, 0x81);
    EXPECT_TRUE(serial.tick(Serial::kTicksPerTransfer));

    EXPECT_EQ(serial.read(Serial::kControl), 0x7F);
    EXPECT_EQ(serial.read(Serial::kData), 0xFF);
}

TEST(Serial, InterruptIsReportedOncePerTransfer) {
    Serial serial;
    serial.write(Serial::kControl, 0x81);
    EXPECT_TRUE(serial.tick(Serial::kTicksPerTransfer));
    EXPECT_FALSE(serial.tick(Serial::kTicksPerTransfer));
}

TEST(Serial, ExternalClockNeverCompletesWithNothingPluggedIn) {
    Serial serial;
    serial.write(Serial::kData, 0x12);
    serial.write(Serial::kControl, 0x80);

    EXPECT_FALSE(serial.tick(10 * Serial::kTicksPerTransfer));
    EXPECT_EQ(serial.output(), "");
    EXPECT_EQ(serial.read(Serial::kControl), 0xFE);
    EXPECT_EQ(serial.read(Serial::kData), 0x12);
}

TEST(Serial, IdlePortDoesNothing) {
    Serial serial;
    EXPECT_FALSE(serial.tick(100000));
    EXPECT_EQ(serial.output(), "");
}

}  // namespace
