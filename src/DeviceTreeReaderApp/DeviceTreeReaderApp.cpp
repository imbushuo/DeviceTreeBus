#include <iostream>
#include <fstream>
#include <string>

#include <stdio.h>
#include <libfdt.h>

using namespace std;

int main(int argc, char* argv[])
{
    if (argc <= 1) return 0;

    ifstream dtbfile(argv[1], ios::binary | ios::ate);
    auto size = dtbfile.tellg();
    dtbfile.seekg(0, ios::beg);

    char* dtbBuffer = NULL;
    dtbBuffer = reinterpret_cast<char*>(malloc(size));
    if (dtbBuffer == NULL)
    {
        return -errno;
    }

    if (!dtbfile.read(dtbBuffer, size))
    {
        dtbfile.close();
        return -1;
    }

    int err;
    err = fdt_check_header(dtbBuffer);
    if (err != 0) {
        fprintf(stderr, "Invalid device tree binary: %s\n", fdt_strerror(err));
        return 1;
    }

    int nodeoffset = 0;
    int depth = 0;
    while (nodeoffset >= 0 && depth >= 0) {
        printf("%d %d ", nodeoffset, depth);
        const char* nodename = fdt_get_name(dtbBuffer, nodeoffset, NULL);
        if (nodename == NULL) {
            fprintf(stderr, "Failed to get node name\n");
            break;
        }
        printf("Node name: %s ", nodename);

        const auto prop = fdt_get_property(dtbBuffer, nodeoffset, "compatible", NULL);
        if (prop != NULL && prop->len > 0) {
            printf("has compatible property: %s", prop->data);
        }

        int tmpOffset = 0, tmpDepth = 0;
        tmpOffset = fdt_next_node(dtbBuffer, nodeoffset, &tmpDepth);
        if (tmpOffset >= 0) tmpDepth = fdt_node_depth(dtbBuffer, tmpOffset);
        bool busNode = (tmpOffset >= 0 && tmpDepth > depth) ? true : false;
        if (busNode) printf(" is bus node");

        printf("\n");

#if 0
        int propoffset = fdt_first_property_offset(dtbBuffer, nodeoffset);
        while (propoffset >= 0) {
            int len;
            auto property = fdt_get_property_by_offset(dtbBuffer, propoffset, &len);
            if (property == NULL) {
                break;
            }

            const char* propertyName = fdt_string(dtbBuffer, fdt32_to_cpu(property->nameoff));
            if (propertyName != NULL)
            {
                int proplen = fdt32_to_cpu(property->len);

                printf("- Property: %s = ", propertyName);
                for (int i = 0; i < proplen; i++) {
                    const uint8_t c = property->data[i];

                    if (isprint(c))
                        putc(c, stdout);
                    else
                        printf("%02x", c);
                }
                printf("\n");
            }

            propoffset = fdt_next_property_offset(dtbBuffer, propoffset);
        }
#endif

        nodeoffset = fdt_next_node(dtbBuffer, nodeoffset, &depth);
    }

    free(dtbBuffer);
    return 0;
}
