/* mbed Microcontroller Library
 * Copyright (c) 2006-2013 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "SerialBase.h"
#include "wait_api.h"

#if DEVICE_SERIAL

namespace mbed {

SerialBase::SerialBase(PinName tx, PinName rx) : _serial(), _baud(9600),
                                                 _thunk_irq(this), _tx_usage(DMA_USAGE_NEVER),
                                                 _rx_usage(DMA_USAGE_NEVER) {
    serial_init(&_serial, tx, rx);
    serial_irq_handler(&_serial, SerialBase::_irq_handler, (uint32_t)this);
}

void SerialBase::baud(int baudrate) {
    serial_baud(&_serial, baudrate);
    _baud = baudrate;
}

void SerialBase::format(int bits, Parity parity, int stop_bits) {
    serial_format(&_serial, bits, (SerialParity)parity, stop_bits);
}

int SerialBase::readable() {
    return serial_readable(&_serial);
}


int SerialBase::writeable() {
    return serial_writable(&_serial);
}

void SerialBase::attach(void (*fptr)(void), IrqType type) {
    if (fptr) {
        _irq[type].attach(fptr);
        serial_irq_set(&_serial, (SerialIrq)type, 1);
    } else {
        serial_irq_set(&_serial, (SerialIrq)type, 0);
    }
}

void SerialBase::_irq_handler(uint32_t id, SerialIrq irq_type) {
    SerialBase *handler = (SerialBase*)id;
    handler->_irq[irq_type].call();
}

int SerialBase::_base_getc() {
    return serial_getc(&_serial);
}

int SerialBase::_base_putc(int c) {
    serial_putc(&_serial, c);
    return c;
}

void SerialBase::send_break() {
  // Wait for 1.5 frames before clearing the break condition
  // This will have different effects on our platforms, but should
  // ensure that we keep the break active for at least one frame.
  // We consider a full frame (1 start bit + 8 data bits bits +
  // 1 parity bit + 2 stop bits = 12 bits) for computation.
  // One bit time (in us) = 1000000/_baud
  // Twelve bits: 12000000/baud delay
  // 1.5 frames: 18000000/baud delay
  serial_break_set(&_serial);
  wait_us(18000000/_baud);
  serial_break_clear(&_serial);
}

#if DEVICE_SERIAL_FC
void SerialBase::set_flow_control(Flow type, PinName flow1, PinName flow2) {
    FlowControl flow_type = (FlowControl)type;
    switch(type) {
        case RTS:
            serial_set_flow_control(&_serial, flow_type, flow1, NC);
            break;

        case CTS:
            serial_set_flow_control(&_serial, flow_type, NC, flow1);
            break;

        case RTSCTS:
        case Disabled:
            serial_set_flow_control(&_serial, flow_type, flow1, flow2);
            break;

        default:
            break;
    }
}
#endif

#if DEVICE_SERIAL_ASYNCH

int SerialBase::write(uint8_t *buffer, int length, void (*callback)(int), int event)
{
    if (serial_tx_active(&_serial)) {
        return -1; // transaction ongoing
    }
    serial_tx_buffer_set(&_serial, buffer, length, 8);
    return start_write(event, callback);
}

int SerialBase::write(uint16_t *buffer, int length, void (*callback)(int), int event)
{
    if (serial_tx_active(&_serial)) {
        return -1; // transaction ongoing
    }
    serial_tx_buffer_set(&_serial, buffer, length, 16);
    return start_write(event, callback);
}

int SerialBase::start_write(int event, void (*callback)(int))
{
    _tx_user_callback = callback;
    
    _thunk_irq.callback(&SerialBase::interrupt_handler_asynch);
    serial_tx_enable_event(&_serial, event, true);
    serial_start_write_asynch(&_serial, (void *)_thunk_irq.entry(), _tx_usage);
    return 0;
}

void SerialBase::abort_write(void)
{
    serial_tx_abort_asynch(&_serial);
}

void SerialBase::abort_read(void)
{
    serial_rx_abort_asynch(&_serial);
}

int SerialBase::set_dma_usage_tx(DMAUsage usage)
{
    if (serial_tx_active(&_serial)) {
        return -1;
    }
    _tx_usage = usage;
    return 0;
}

int SerialBase::set_dma_usage_rx(DMAUsage usage)
{
    if (serial_tx_active(&_serial)) {
        return -1;
    }
    _rx_usage = usage;
    return 0;
}

int SerialBase::read(uint8_t *buffer, int length, void (*callback)(int), int event, uint8_t char_match)
{
    if (serial_rx_active(&_serial)) {
        return -1; // transaction ongoing
    }
    serial_rx_buffer_set(&_serial, buffer, length, 8);
    return start_read(event, callback, char_match);
}


int SerialBase::read(uint16_t *buffer, int length, void (*callback)(int), int event, uint8_t char_match)
{
    if (serial_rx_active(&_serial)) {
        return -1; // transaction ongoing
    }
    serial_rx_buffer_set(&_serial, buffer, length, 16);
    return start_read(event, callback, char_match);
}


int SerialBase::start_read(int event, void (*callback)(int), uint8_t char_match)
{
    serial_set_char_match(&_serial, char_match);
    _rx_user_callback = callback;
    
    _thunk_irq.callback(&SerialBase::interrupt_handler_asynch);
    serial_rx_enable_event(&_serial, event, true);
    serial_start_read_asynch(&_serial, (void *)_thunk_irq.entry(), _rx_usage);
    return 0;
}

void SerialBase::interrupt_handler_asynch(void)
{
    int rx_event = serial_rx_irq_handler_asynch(&_serial);
    if (_rx_user_callback && rx_event) {
        _rx_user_callback(rx_event);
    }

    int tx_event = serial_tx_irq_handler_asynch(&_serial);
    if (_tx_user_callback && tx_event) {
        _tx_user_callback(tx_event);
    }
}

#endif

} // namespace mbed

#endif
