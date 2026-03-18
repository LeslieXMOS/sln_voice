#include <platform.h>
#include <xcore/port.h>

extern void * readFlashDataPage(unsigned addr);
int dpVersion;
unsigned dfu_img_addr = 0xFFFFFFFF;
unsigned app_img_addr = 0xFFFFFFFF;

void init (void) {
  void* ptr = readFlashDataPage(0);
  dpVersion = *(int*) ptr;
}

int checkCandidateImageVersion(int v) {
//   return v == dpVersion;
    return 1;
}

void recordCandidateImage(int v, unsigned adr) {
    if (v == 0) {
        dfu_img_addr = adr;
    } else {
        app_img_addr = adr;
    }
}

unsigned reportSelectedImage(void) {
    int button_val;
    port_t dfu_button = XS1_PORT_8D;
    port_enable(dfu_button);
    button_val = port_in(dfu_button);
    port_disable(dfu_button);
    if (button_val & (1<<5)) {
        if (app_img_addr != 0xFFFFFFFF) {
            return app_img_addr;
        }
    }
    return dfu_img_addr;
}