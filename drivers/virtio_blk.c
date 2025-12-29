#include <stdint.h>

#define VIRTIO_MMIO_BASE        0x10001000UL
#define MMIO_REG(offset)        (*(volatile uint32_t *)(VIRTIO_MMIO_BASE + (offset)))

#define VIRTIO_MMIO_MAGIC_VALUE 0x000
#define VIRTIO_MMIO_VERSION     0x004
#define VIRTIO_MMIO_DEVICE_ID   0x008
#define VIRTIO_MMIO_VENDOR_ID   0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028   /* legacy only */
#define VIRTIO_MMIO_QUEUE_SEL   0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM   0x038
#define VIRTIO_MMIO_QUEUE_ALIGN 0x03c      /* legacy only */
#define VIRTIO_MMIO_QUEUE_PFN   0x040      /* legacy only */
#define VIRTIO_MMIO_QUEUE_READY 0x044      /* modern */
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064
#define VIRTIO_MMIO_STATUS      0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW  0x080  /* modern */
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_DRIVER_DESC_LOW 0x090
#define VIRTIO_MMIO_DRIVER_DESC_HIGH 0x094
#define VIRTIO_MMIO_DEVICE_DESC_LOW 0x0a0
#define VIRTIO_MMIO_DEVICE_DESC_HIGH 0x0a4

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED      0x80

#define VIRTIO_DEVICE_ID_BLOCK   2
#define VIRTIO_F_VERSION_1      32

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

#define VIRTIO_DEBUG 0

#define VQ_SIZE 8
#define VRING_ALIGN 4096

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VQ_SIZE];
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[VQ_SIZE];
} __attribute__((packed));

struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

static uint8_t vq_mem[8192] __attribute__((aligned(4096)));
static struct virtq_desc *desc;
static volatile struct virtq_avail *avail;
static volatile struct virtq_used *used;

static uint16_t avail_idx = 0;
static uint16_t used_idx = 0;
static volatile uint8_t status_byte = 0;
static struct virtio_blk_req header;
static uint32_t virtio_version = 0;

/* minimal print declaration */
void print(const char *s);
void print_hex(uint64_t num);

static void write_status(uint32_t val)
{
    MMIO_REG(VIRTIO_MMIO_STATUS) = val;
}

static void add_status(uint32_t val)
{
    uint32_t cur = MMIO_REG(VIRTIO_MMIO_STATUS);
    MMIO_REG(VIRTIO_MMIO_STATUS) = cur | val;
}

static uint64_t ptr_to_phys(const volatile void *p)
{
    return (uint64_t)(uintptr_t)p;
}

static void setup_vring_layout(void)
{
    /* 清零 vring 内存，避免脏数据影响设备解析 */
    for (uint32_t i = 0; i < sizeof(vq_mem); i++) vq_mem[i] = 0;
    desc = (struct virtq_desc *)vq_mem;
    uint32_t desc_bytes = sizeof(struct virtq_desc) * VQ_SIZE;
    uint32_t avail_offset = desc_bytes;
    avail = (volatile struct virtq_avail *)(vq_mem + avail_offset);

    uint32_t used_offset = (avail_offset + sizeof(struct virtq_avail) + VRING_ALIGN - 1) & ~(VRING_ALIGN - 1);
    used = (volatile struct virtq_used *)(vq_mem + used_offset);
}

int virtio_blk_init(void)
{
    if (MMIO_REG(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976) {
        print("virtio: bad magic\n");
        return -1;
    }
    virtio_version = MMIO_REG(VIRTIO_MMIO_VERSION);
    print("virtio version: "); print_hex(virtio_version); print("\n");
    uint32_t dev_id = MMIO_REG(VIRTIO_MMIO_DEVICE_ID);
    print("virtio device id: "); print_hex(dev_id); print("\n");
    uint32_t vendor_id = MMIO_REG(VIRTIO_MMIO_VENDOR_ID);
    print("virtio vendor: "); print_hex(vendor_id); print("\n");
    if (virtio_version != 1 && virtio_version != 2) {
        print("virtio: unsupported version\n");
        return -1;
    }
    if (MMIO_REG(VIRTIO_MMIO_DEVICE_ID) != VIRTIO_DEVICE_ID_BLOCK) {
        print("virtio: not a block device\n");
        return -1;
    }

    setup_vring_layout();

    write_status(0);
    add_status(VIRTIO_STATUS_ACKNOWLEDGE);
    add_status(VIRTIO_STATUS_DRIVER);

    /* feature 协商：modern 设备需要 VERSION_1，legacy 设备清零 */
    MMIO_REG(VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 0;
    MMIO_REG(VIRTIO_MMIO_DRIVER_FEATURES) = 0;
    if (virtio_version == 2) {
        MMIO_REG(VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 1;
        MMIO_REG(VIRTIO_MMIO_DRIVER_FEATURES) = (1u << (VIRTIO_F_VERSION_1 - 32));
    }
    add_status(VIRTIO_STATUS_FEATURES_OK);
    if ((MMIO_REG(VIRTIO_MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0) {
        print("virtio: FEATURES_OK rejected\n");
        return -1;
    }

    MMIO_REG(VIRTIO_MMIO_QUEUE_SEL) = 0;
    uint32_t max = MMIO_REG(VIRTIO_MMIO_QUEUE_NUM_MAX);
    print("virtio queue max: "); print_hex(max); print("\n");
    if (max == 0 || max < VQ_SIZE) {
        print("virtio: queue too small\n");
        add_status(VIRTIO_STATUS_FAILED);
        return -1;
    }
    MMIO_REG(VIRTIO_MMIO_QUEUE_NUM) = VQ_SIZE;

    uint64_t desc_addr = ptr_to_phys(desc);
    uint64_t avail_addr = ptr_to_phys(avail);
    uint64_t used_addr = ptr_to_phys(used);
    print("desc addr: "); print_hex(desc_addr); print("\n");
    print("avail addr: "); print_hex(avail_addr); print("\n");
    print("used addr: "); print_hex(used_addr); print("\n");

    if (virtio_version == 1) {
        /* 传统接口：设置 align + PFN */
            MMIO_REG(VIRTIO_MMIO_GUEST_PAGE_SIZE) = 4096;
        MMIO_REG(VIRTIO_MMIO_QUEUE_ALIGN) = 4096;
        MMIO_REG(VIRTIO_MMIO_QUEUE_PFN) = (uint32_t)(desc_addr >> 12);
    } else {
        /* 现代接口 */
        MMIO_REG(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint32_t)desc_addr;
        MMIO_REG(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint32_t)(desc_addr >> 32);
        MMIO_REG(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint32_t)avail_addr;
        MMIO_REG(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint32_t)(avail_addr >> 32);
        MMIO_REG(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint32_t)used_addr;
        MMIO_REG(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint32_t)(used_addr >> 32);
    }
    MMIO_REG(VIRTIO_MMIO_QUEUE_READY) = 1;

    avail_idx = 0;
    used_idx = 0;
    avail->flags = 0;
    avail->idx = 0;
    used->flags = 0;
    used->idx = 0;

    add_status(VIRTIO_STATUS_DRIVER_OK);
    uint32_t st = MMIO_REG(VIRTIO_MMIO_STATUS);
    print("virtio status: "); print_hex(st); print("\n");
    print("virtio init done\n");
    return 0;
}

int virtio_blk_read_sector(uint64_t sector, void *buf)
{
    /* 描述符 0: 请求头 */
    header.type = 0; /* VIRTIO_BLK_T_IN */
    header.reserved = 0;
    header.sector = sector;

    desc[0].addr = ptr_to_phys(&header);
    desc[0].len = sizeof(header);
    desc[0].flags = VRING_DESC_F_NEXT;
    desc[0].next = 1;

    /* 描述符 1: 数据缓冲区（设备写入） */
    desc[1].addr = ptr_to_phys(buf);
    desc[1].len = 512;
    desc[1].flags = VRING_DESC_F_NEXT | VRING_DESC_F_WRITE;
    desc[1].next = 2;

    /* 描述符 2: 状态字节（设备写入） */
    status_byte = 0xFF;
    desc[2].addr = ptr_to_phys(&status_byte);
    desc[2].len = 1;
    desc[2].flags = VRING_DESC_F_WRITE;
    desc[2].next = 0;

    /* 放入 avail 队列 */
    avail->ring[avail_idx % VQ_SIZE] = 0;
    avail_idx++;
    avail->idx = avail_idx;

    /* 通知设备 */
    asm volatile("fence w,w" ::: "memory");
    MMIO_REG(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

    /* 等待 used ring 更新，设置超时避免死等 */
    if (VIRTIO_DEBUG) {
        print("virtio avail idx: "); print_hex(avail_idx); print(" used idx: "); print_hex(used_idx); print("\n");
    }
    uint32_t spins = 0;
    while (used->idx == used_idx) {
        spins++;
        if (spins > 10000000) {
            uint32_t isr = MMIO_REG(VIRTIO_MMIO_INTERRUPT_STATUS);
            uint32_t ready = MMIO_REG(VIRTIO_MMIO_QUEUE_READY);
            print("virtio: request timeout isr="); print_hex(isr); print(" ready="); print_hex(ready); print("\n");
            if (isr) MMIO_REG(VIRTIO_MMIO_INTERRUPT_ACK) = isr;
            return -1;
        }
    }
    used_idx = used->idx;

    uint32_t isr = MMIO_REG(VIRTIO_MMIO_INTERRUPT_STATUS);
    if (isr) MMIO_REG(VIRTIO_MMIO_INTERRUPT_ACK) = isr;

    if (VIRTIO_DEBUG) {
        print("virtio used idx after: "); print_hex(used_idx); print(" status byte: "); print_hex(status_byte); print("\n");
    }

    return status_byte == 0 ? 0 : -1;
}
