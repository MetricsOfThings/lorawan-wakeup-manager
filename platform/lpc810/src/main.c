void lpc810_gpio_init(void);
void lpc810_timer_init(void);

int main(void) {
    lpc810_gpio_init();
    lpc810_timer_init();

    while (1) { }
}
