/*
 * 	startup.c
 */

 
 #include "syscfg.h"
 #include "exti.h"
 #include "NVIC.h"
 #include "gpio.h"
 
#define B_E 0x40
#define B_RST 0x20
#define B_CS2 0x10
#define B_CS1 8
#define B_SELECT 4
#define B_RW 2
#define B_RS 1

#define LCD_ON 0x3F		// Display on
#define LCD_OFF 0x3E		// Display off
#define LCD_SET_ADD 0x40	// Set horizontal coordinate
#define LCD_SET_PAGE 0xB8	// Set vertical coordinate
#define LCD_DISP_START 0xC0 // Start address
#define LCD_BUSY 0x80		// Read busy status

#define STK_CTRL ((volatile unsigned int *) (0xE000E010))
#define STK_LOAD ((volatile unsigned int *) (0xE000E014))
#define STK_VAL ((volatile unsigned int *) (0xE000E018))

 
 #define EXTI2_IRQVEC ((void (**) (void) ) 0x2001C060)
 #define EXTI1_IRQVEC ((void (**) (void) ) 0x2001C05C)
 #define EXTI0_IRQVEC ((void (**) (void) ) 0x2001C058)
 
 #define NVIC_EXTI2_IRQ_BPOS (1<<8)
 #define NVIC_EXTI1_IRQ_BPOS (1<<7)
 #define NVIC_EXTI0_IRQ_BPOS (1<<6)
 
 #define EXTI2_IRQ_BPOS (1<<2)
 #define EXTI1_IRQ_BPOS (1<<1)
 #define EXTI0_IRQ_BPOS (1<<0)
 #define EXTI_IRQ_BPOS EXTI0_IRQ_BPOS|EXTI1_IRQ_BPOS|EXTI2_IRQ_BPOS;
 
void startup(void) __attribute__((naked)) __attribute__((section (".start_section")) );

void startup ( void )
{
__asm volatile(
	" LDR R0,=0x2001C000\n"		/* set stack */
	" MOV SP,R0\n"
	" BL main\n"				/* call main */
	".L1: B .L1\n"				/* never return */
	) ;
}

int count = 0;
void delay_250ns(void){
	/* SystemCoreClock = 168000000 */
	*STK_CTRL = 0;
	*STK_LOAD = ( (168/4) -1);
	*STK_VAL = 0;
	*STK_CTRL = 5;
	while( (*STK_CTRL & 0x10000)== 0 ){}
	*STK_CTRL = 0;
	
}

void delay_500ns(void){
	delay_250ns();
	delay_250ns();
}

void delay_micro(unsigned int us){
	while(us--) {
		delay_250ns();
		delay_250ns();
		delay_250ns();
		delay_250ns();
	}
}

void delay_milli(unsigned int ms){
	#ifdef SIMULATOR
		while(ms--)
			delay_micro(1);
	#else
		while(ms--)
			delay_micro(1);
	#endif
}

typedef unsigned char uint8_t;
static void graphic_ctrl_bit_set(uint8_t x){
//adressera grafik-displayen och ettställ de bitar som är 1 i x
	uint8_t c;
	c = GPIO_E.odrLow;
	c &= ~B_SELECT;
	c |= (~B_SELECT & x);
	GPIO_E.odrLow = c;
}

static void graphic_ctrl_bit_clear(uint8_t x){
//adressera grafik-displayen och nollställ de bitar som är 1 i x
	uint8_t c;
	c = GPIO_E.odrLow;
	c &= ~B_SELECT;
	c &= ~x;
	GPIO_E.odrLow = c;
}

static void select_controller(uint8_t controller){
	switch(controller){
		case 0:
			graphic_ctrl_bit_clear(B_CS1|B_CS2);
			break;
		case B_CS1 :
			graphic_ctrl_bit_set(B_CS1);
			graphic_ctrl_bit_clear(B_CS2);
			break;
		case B_CS2 :
			graphic_ctrl_bit_set(B_CS2);
			graphic_ctrl_bit_clear(B_CS1);
			break;
		case B_CS1|B_CS2 :
			graphic_ctrl_bit_set(B_CS1|B_CS2);
			break;
	}
}

static void graphic_wait_ready(void) {
	uint8_t c;
	graphic_ctrl_bit_clear(B_E);
	*portModer = 0x00005555; // 15-8 inputs, 7-0 outputs
	graphic_ctrl_bit_clear(B_RS);
	graphic_ctrl_bit_set(B_RW);
	delay_500ns();
	while(1) {
		graphic_ctrl_bit_set(B_E);
		delay_500ns();
		c = *portIdrHigh & LCD_BUSY;
		graphic_ctrl_bit_clear(B_E);
		delay_500ns();
		if( c == 0 ) break;
	}
	*portModer = 0x55555555; // 15-0 outputs
}

static uint8_t graphic_read(uint8_t controller) {
	uint8_t c;
	graphic_ctrl_bit_clear(B_E);
	*portModer = 0x00005555; // 15-8 inputs, 7-0 outputs
	graphic_ctrl_bit_set(B_RS|B_RW);
	select_controller(controller);
	delay_500ns();
	graphic_ctrl_bit_set(B_E);
	delay_500ns();
	c = *portIdrHigh;
	graphic_ctrl_bit_clear(B_E);
	*portModer = 0x55555555; // 15-0 outputs
	if( controller & B_CS1 ) {
		select_controller(B_CS1);
		graphic_wait_ready();
	}
	if( controller & B_CS2 ) {
		select_controller(B_CS2);
		graphic_wait_ready();
	}
	return c;
}

static uint8_t graphic_read_data(uint8_t controller) {
	graphic_read(controller);
	return graphic_read(controller);
}

static void graphic_write(uint8_t value, uint8_t controller) {
	*portOdrHigh = value;
	select_controller(controller);
	delay_500ns();
	graphic_ctrl_bit_set(B_E);
	delay_500ns();
	graphic_ctrl_bit_clear( B_E );
	if(controller & B_CS1) {
		select_controller( B_CS1);
		graphic_wait_ready();
	}
	if(controller & B_CS2) {
       select_controller( B_CS2);
       graphic_wait_ready();
    }
}

static void graphic_write_command(uint8_t command, uint8_t controller) {
	graphic_ctrl_bit_clear(B_E);
	select_controller(controller);
	graphic_ctrl_bit_clear(B_RS|B_RW);
	graphic_write(command, controller);
}

static void graphic_write_data(uint8_t data, uint8_t controller) {
	graphic_ctrl_bit_clear(B_E);
	select_controller(controller);
	graphic_ctrl_bit_set(B_RS);
    graphic_ctrl_bit_clear(B_RW);
    graphic_write(data, controller);
}

void graphic_clear_screen(void) {
	uint8_t i, j;
	for(j = 0; j < 8; j++) {
		graphic_write_command(LCD_SET_PAGE | j, B_CS1|B_CS2);
		graphic_write_command(LCD_SET_ADD | 0, B_CS1|B_CS2);
		for(i = 0; i <= 63; i++){
			graphic_write_data(0, B_CS1|B_CS2);
		} 
	}
}

void graphic_initialize(void) {
	graphic_ctrl_bit_set(B_E);
	delay_micro(10);
	graphic_ctrl_bit_clear(B_CS1|B_CS2|B_RST|B_E);
	delay_milli(30);
	graphic_ctrl_bit_set(B_RST);
	delay_milli(100);
	graphic_write_command(LCD_OFF, B_CS1|B_CS2);
	graphic_write_command(LCD_ON, B_CS1|B_CS2);
	graphic_write_command(LCD_DISP_START, B_CS1|B_CS2);
	graphic_write_command(LCD_SET_ADD, B_CS1|B_CS2);
	graphic_write_command(LCD_SET_PAGE, B_CS1|B_CS2);
	select_controller(0);
}

void pixel(int x, int y, int set) {
	uint8_t mask, c, controller;
	int index;
	
	if((x < 1) || (y < 1) || (x > 128) || (y > 64)) return;
	
	index = (y-1)/8;
	mask = (1<<((y-1)%8));
	/*switch( (y-1)%8 ) {
		case 0: mask = 1;break;
		case 1: mask = 2; break;
		case 2: mask = 4; break;
		case 3: mask = 8; break;
		case 4: mask = 0x10; break;
		case 5: mask = 0x20; break;
		case 6: mask = 0x40; break;
		case 7: mask = 0x80; break;
	}*/
	if(set == 0)
		mask = ~mask;
	if(x > 64){
		controller = B_CS2;
		x = x - 65;
	} else {
		controller = B_CS1;
		x = x-1;
	}
	graphic_write_command(LCD_SET_ADD | x, controller );
	graphic_write_command(LCD_SET_PAGE | index, controller );
	c = graphic_read_data(controller);
	graphic_write_command(LCD_SET_ADD | x, controller);
	
	if(set)
		mask = mask | c;
	else
		mask = mask & c;
	
	graphic_write_data(mask, controller);
}

typedef struct tPoint {
	unsigned char x;
	unsigned char y;
} POINT;

#define MAX_POINTS 20
typedef struct tGeometry {
	int numpoints;
	int sizex;
	int sizey;
	POINT px[ MAX_POINTS ];
} GEOMETRY, *PGEOMETRY;

typedef struct tObj {
	PGEOMETRY geo;
	int dirx, diry;
	int posx, posy;
	void (* draw ) (struct tObj *);
	void (* clear ) (struct tObj *);
	void (* move ) (struct tObj *);
	void (* set_speed ) (struct tObj *, int, int);
} OBJECT, *POBJECT;

GEOMETRY ball_geometry =
{
	12, 
	4, 4, 
	{ {0,1},{0,2},{1,0},{1,1},{1,2},{1,3},{2,0},{2,1},{2,2},{2,3},{3,1},{3,2} }
};

void draw_object(POBJECT p) {
	for(int i=0; i < p->geo->numpoints; i++){
		pixel(p->posx + p->geo->px[i].x, p->posy + p->geo->px[i].y, 1);
	}
}

void clear_object(POBJECT p) {
	for(int i=0; i < p->geo->numpoints; i++){
		pixel(p->posx + p->geo->px[i].x, p->posy + p->geo->px[i].y, 0);
	}
}

void set_object_speed(POBJECT p, int speedx, int speedy) {
	p->dirx = speedx;
	p->diry = speedy;
}

void move_object(POBJECT p) {
	clear_object(p);
	
	p->posx += p->dirx;
	p->posy += p->diry;
	
	int w = p->geo->sizex;
	int h = p->geo->sizey;
	
	if(p->dirx < 0 && p->posx < 1){
		p->dirx = -p->dirx;
		p->posx = 1;
	}
	
	if(p->dirx > 0 && p->posx > 128 - w){
		p->dirx = -p->dirx;
		p->posx = 128 - w;
	}
	
	if(p->diry < 0 && p->posy < 1){
		p->diry = -p->diry;
		p->posy = 1;
	}
	
	if(p->diry > 0 && p->posy > 64 - h){
		p->diry = -p->diry;
		p->posy = 64 - h;
	}

	
	
	draw_object(p);
}

static OBJECT ball =
{
	&ball_geometry,	//Geometri för en boll
	0,0,			//riktingskoordinater
	1,1,			//startposition
	draw_object,
	clear_object,
	move_object,
	set_object_speed
};

void keyboardActivate(unsigned int row){
	//Aktivera angiven rad hos tangentbordet eller deaktivera samtliga
	
	switch(row){
		case 1: GPIO_ODR_HIGH = 0x10; break;
		case 2: GPIO_ODR_HIGH = 0x20; break;
		case 3: GPIO_ODR_HIGH = 0x40; break;
		case 4: GPIO_ODR_HIGH = 0x80; break;
		case 0: GPIO_ODR_HIGH = 0x00; break;
	}
}

int keyboardGetColumn(void){
	//Om någon tangent i den aktiverade raden är nedtryckt
	// Flex och Mårdhor är bäst
	// returneras dess kolumnnummer. Annars 0.
	
	unsigned char c;
	c = GPIO_IDR_HIGH;
	if(c & 0x8){return 4;}
	if(c & 0x4){return 3;}
	if(c & 0x2){return 2;}
	if(c & 0x1){return 1;}
	return 0;
	}

unsigned char keyboard(void){
	unsigned char key[] = {1,2,3,0xA,4,5,6,0xB,7,8,9,0xC,0xE,0,0xF,0xD};
	int row, col;
	for(row=1; row<=4; row++){
		keyboardActivate(row);
		if(col = keyboardGetColumn()){
			keyboardActivate(0);
			return key[4*(row-1) + (col-1)];
		}
	}
	keyboardActivate(0);
	return 0xFF;
}

void main(void){
	init_app();
	graphic_initialize();
	graphic_clear_screen();
	moveball();
	for(int i = 0; i<128; i++){
		pixel(i,20,1);
	}
	for(int i = 0; i<64; i++){
		pixel(20,i,1);
	}
	delay_milli(500);
	graphic_clear_screen();


}

void moveball(){
	POBJECT p = &ball;
	init_app();
	graphic_initialize();
	graphic_clear_screen();
	
	p->set_speed(p,2,-2);
	while(1){
		p->move(p);
	/*	int x = p->posx;
		if(x == 0 || x >= 127 )
			p->set_speed(p, p -> dirx * (-1), p -> diry);
			
		int y = p->posy;
		if(y == 0 || y >= 63 )
	

		p->set_speed(p, p->dirx, p->diry * (-1));
	*/
	}

	
}
	
void controlball(){
		char c;
	POBJECT p = &ball;
	init_app();
	graphic_initialize();
	graphic_clear_screen();
	
	while(1){
		p->move(p);
		delay_milli(20);
		c = keyboard();
		int dx = 0, dy = 0;
		switch(c) {
			case 6: 
				dx = 2;
			case 4:
				dx = -2;
			case 2:
				dy = -2;
			case 8:
				dy = 2;
		}
		p->set_speed(p,dx,dy);
}



unsigned char keyb(){
	
	unsigned char key[4][4]  = {{1, 2, 3, 0xA}, {4, 5, 6, 0xB}, {7, 8, 9, 0xC}, {0xE, 0, 0xF, 0xD}};
	
	for(int row = 1 ; row < 5 ; row++){
		ActivateRow(row);
		int column = ReadColumn();
		if (column != 0){
			ActivateRow(0);
			return key[row-1][column-1];
		}
	}
	ActivateRow(0);
	return 0xFF;	
}

void ActivateRow(unsigned int row){
	if(row)
		GPIO_D.odrHigh = (0x08<<row);
	else
		GPIO_D.odrHigh = 0;
		
	/*switch(row){
		case 1: *GPIO_ODR_HIGH = 0x10; break;
		case 2: *GPIO_ODR_HIGH = 0x20; break;
		case 3: *GPIO_ODR_HIGH = 0x40; break;
		case 4: *GPIO_ODR_HIGH = 0x80; break;
		case 0: *GPIO_ODR_HIGH = 0x00; break;
	}*/
}

int ReadColumn(){
	unsigned char c; 
	c = *GPIO_IDR_HIGH;
	
	if(c & 0x8) return 4;
	if(c & 0x4) return 3;
	if(c & 0x2) return 2;
	if(c & 0x1) return 1;
	return 0;
	
}

void irq0_handler(void){
	if(EXTI.pr & EXTI0_IRQ_BPOS){
		count++;
		resetflag(EXTI0_IRQ_BPOS);
	}
}

void irq1_handler(void){
	if(EXTI.pr & EXTI1_IRQ_BPOS){
		count = 0;
		resetflag(EXTI1_IRQ_BPOS);
	}
}

void irq2_handler(void){
	if(EXTI.pr & EXTI2_IRQ_BPOS){		
		GPIO_D.odrHigh = ~GPIO_D.odrHigh;
		resetflag(EXTI2_IRQ_BPOS);
	}
}

void resetflag(unsigned int flag){
	GPIO_E.odrLow |= (flag<<4);
	GPIO_E.odrLow &= ~(flag<<4);
	EXTI.pr |= flag;

}

void init_app(void){
	//PD0-7 som utport för visningsenhet
	GPIO_D.moder = 0x55555555;
	GPIO_E.moder = 0x1500;
	
	//PEx till avbrottslina EXTIx
	SYSCFG.exticr1 &= ~0x0FFF;
	SYSCFG.exticr1 |= 0x0444;
	
	//..Generera avbrott... negativ flank
	
	EXTI.imr |= EXTI_IRQ_BPOS;
	EXTI.ftsr &= ~EXTI_IRQ_BPOS;
	EXTI.rtsr |= EXTI_IRQ_BPOS;
	
	*EXTI0_IRQVEC = irq0_handler;
	*EXTI1_IRQVEC = irq1_handler;
	*EXTI2_IRQVEC = irq2_handler;
	
	
	NVIC.iser0 |= NVIC_EXTI0_IRQ_BPOS|NVIC_EXTI1_IRQ_BPOS|NVIC_EXTI2_IRQ_BPOS;
	
	#ifdef USBDM
		*((unsigned long *) 0x40023830) = 0x18;
		*((unsigned long *) 0x40023844) |= 0x4000;
		*((unsigned long *) 0xE000ED08) = 0x2001C000;
	#endif
}

void main(void)
{
}

