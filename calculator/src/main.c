/*
 * Copyright (c) 2022 Libre Solar Technologies GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

//MY SECTION


/* change this to any other UART peripheral if desired */
#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)

#define MSG_SIZE 32

#define MAX_EXP 20

/* queue to store up to 10 messages (aligned to 4-byte boundary) */
K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);

static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

/* receive buffer used in UART ISR callback */
static char rx_buf[MSG_SIZE];
static int rx_buf_pos;

struct expression{
	char operation;
	int result;
	int r_operand;
	int l_operand;
	};

static struct expression exp_chain[MAX_EXP];

void parser(char *str){
	int str_len = strlen(str);
	
	char *tmp;
	uint8_t operand_size = 0;
	uint8_t num_of_exp = 0;
	
	for (int j = 0; j < str_len; j++){
		if (strchr("0123456789",str[j]) != NULL){
			tmp[operand_size++] = str[j];	
		}
		else if(strchr("+-*/",str[j]) != NULL){
			switch (str[j]){
				case '+':
					exp_chain[num_of_exp].operation = 1;
					break;
				case '-':
					exp_chain[num_of_exp].operation = 2;
					break;
				case '*':
					exp_chain[num_of_exp].operation = 3;
					break;
				case '/':
					exp_chain[num_of_exp].operation = 4;
					break;
				default:				
			}
			
			tmp[operand_size] = '\0';
			if(num_of_exp==0){
				exp_chain[num_of_exp].l_operand = atoi(tmp);
			}
			else{
				exp_chain[num_of_exp].l_operand = atoi(tmp);
				exp_chain[num_of_exp-1].r_operand = atoi(tmp);
			}
			exp_chain[num_of_exp].result = 0;
			operand_size = 0;
			num_of_exp++;
		}
		if(j == (str_len-1)){
			exp_chain[num_of_exp].r_operand = atoi(tmp);
			operand_size = 0;
		}
		for (int k = num_of_exp; k <= MAX_EXP; k++){
			exp_chain[k].operation=0;
		}
	}
}

void calculate_mult(){
	for (int i = 0; i < MAX_EXP; i++){
		if (exp_chain[i].operation == 3){
			exp_chain[i].result = exp_chain[i].l_operand * exp_chain[i].r_operand;
			
			if(i == 0){
				exp_chain[i+1].l_operand = exp_chain[i].result;
			}
			else{
				exp_chain[i-1].r_operand = exp_chain[i].result;
				exp_chain[i+1].l_operand = exp_chain[i].result;
			}
		}
		
		else if (exp_chain[i].operation == 4){
			exp_chain[i].result = exp_chain[i].l_operand / exp_chain[i].r_operand;
			
			if(i == 0){
				exp_chain[i+1].l_operand = exp_chain[i].result;
			}
			else{
				exp_chain[i-1].r_operand = exp_chain[i].result;
				exp_chain[i+1].l_operand = exp_chain[i].result;
			}
		}	
	}
}

int calculate_add(){
	for (int i = 0; i < MAX_EXP; i++){
		if (exp_chain[i].operation == 1){
			exp_chain[i].result = exp_chain[i].l_operand + exp_chain[i].r_operand;
			if(i==(MAX_EXP-1)){
				return exp_chain[i].result;
			}
			else{
				exp_chain[i+1].l_operand = exp_chain[i].result;
			}
		}
		else if (exp_chain[i].operation == 2){
			exp_chain[i].result = exp_chain[i].l_operand - exp_chain[i].r_operand;
			if(i==(MAX_EXP-1)){
				return exp_chain[i].result;
			}
			else{
				exp_chain[i+1].l_operand = exp_chain[i].result;
			}
		}
		else{
			if(i==(MAX_EXP-1)){
				return exp_chain[i].result;
			}
			else{
				exp_chain[i+1].l_operand = exp_chain[i].l_operand;
			}
		}
	}
	return 0;
}

/*
 * Print a null-terminated string character by character to the UART interface
 */
void print_uart(char *buf)
{
	int msg_len = strlen(buf);

	for (int i = 0; i < msg_len; i++) {
		uart_poll_out(uart_dev, buf[i]);
	}
}

/*
 * Read characters from UART until line end is detected. Afterwards push the
 * data to the message queue.
 */
void serial_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	if (!uart_irq_update(uart_dev)) {
		return;
	}

	if (!uart_irq_rx_ready(uart_dev)) {
		return;
	}

	/* read until FIFO empty */
	while (uart_fifo_read(uart_dev, &c, 1) == 1) {
		if ((c == '=') && rx_buf_pos > 0) {
			uart_poll_out(uart_dev, c);
			
			/* terminate string */
			rx_buf[rx_buf_pos] = '\0';

			/* if queue is full, message is silently dropped */
			k_msgq_put(&uart_msgq, &rx_buf, K_NO_WAIT);
			print_uart("\n");

			/* reset the buffer (it was copied to the msgq) */
			rx_buf_pos = 0;
		}
		else if (rx_buf_pos < (sizeof(rx_buf) - 1)) {
			if (strchr("0123456789+-*/",c) != NULL){
				rx_buf[rx_buf_pos++] = c;
				//char temp[2] = {c,'\0'};
				uart_poll_out(uart_dev, c);
			}
			else{
				uart_poll_out(uart_dev, c);
				print_uart("\nInvalid character.\n");
				for (int i = 0; i < rx_buf_pos; i++) {
					uart_poll_out(uart_dev, rx_buf[i]);
				}
				//rx_buf_pos = 0;
				//print_uart(rx_buf);
			}
			
		}
			/* else: characters beyond buffer size are dropped */
	}
}



int main(void)
{
	char tx_buf[MSG_SIZE];

	if (!device_is_ready(uart_dev)) {
		printk("UART device not found!");
		return 0;
	}

	/* configure interrupt and callback to receive data */
	int ret = uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);

	if (ret < 0) {
		if (ret == -ENOTSUP) {
			printk("Interrupt-driven UART API support not enabled\n");
		} else if (ret == -ENOSYS) {
			printk("UART device does not support interrupt-driven API\n");
		} else {
			printk("Error setting UART callback: %d\n", ret);
		}
		return 0;
	}
	uart_irq_rx_enable(uart_dev);
	
	print_uart("Insert mathematical expression.\r\n");
	
	/* indefinitely wait for input from the user */
	while (k_msgq_get(&uart_msgq, &tx_buf, K_FOREVER) == 0) {
		parser(tx_buf);
		calculate_mult();
		sprintf(rx_buf,"%d",calculate_add());
		print_uart(rx_buf);
		//print_uart(tx_buf);
		print_uart("\r\n");
	}
	return 0;
}
