#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/nvme_ioctl.h>

#define NVME_DEV_PATH "/dev/nvmen0"

int main() {
    int fd;
    struct nvme_admin_cmd cmd;
    struct nvme_passthru_ioctl ioctl_cmd;

    // Open the NVMe device file
    fd = open(NVME_DEV_PATH, O_RDWR);
    if (fd == -1) {
        perror("Failed to open NVMe device");
        return 1;
    }

    // Initialize the NVMe command structure
    memset(&cmd, 0, sizeof(struct nvme_admin_cmd));
    cmd.opcode = NVME_ADMIN_IDENTIFY;
    cmd.nsid = 1; // Namespace ID
    cmd.addr = (unsigned long)&ioctl_cmd;
    cmd.len = sizeof(struct nvme_passthru_ioctl);
    cmd.cdw10 = NVME_IDENTIFY_CTRL;

    // Initialize the IOCTL command structure
    memset(&ioctl_cmd, 0, sizeof(struct nvme_passthru_ioctl));
    ioctl_cmd.opcode = cmd.opcode;
    ioctl_cmd.nsid = cmd.nsid;
    ioctl_cmd.addr = cmd.addr;
    ioctl_cmd.len = cmd.len;
    ioctl_cmd.cdw10 = cmd.cdw10;

    // Send the NVMe command using IOCTL
    if (ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd) == -1) {
        perror("Failed to send NVMe command");
        close(fd);
        return 1;
    }

    // Read the response from the NVMe device
    printf("NVMe Command Result: %d\n", cmd.result);

    // Close the NVMe device file
    close(fd);

    return 0;
}
