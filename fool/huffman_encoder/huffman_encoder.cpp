#include "huffman_encoder.h"

#include <bits/types/FILE.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <queue>

namespace zfish {

HuffmanEncoder::HuffmanEncoder(const std::string& input_filename, bool is_compressed)
    : root_(nullptr),
      is_compressed_(is_compressed),
      input_filename_(input_filename),
      output_filename_(input_filename + ".zLzip"),
      input_filesize_(0),
      output_filesize_(0) {
    for (int i = 0; i < kCodeNum; ++i) {
        points_.at(i).old_code = i;
    }
}

// 释放节点及其子树
void HuffmanEncoder::freeNode(HuffmanTreeNode* node) {
    if (node != nullptr) {
        freeNode(node->left);
        freeNode(node->right);
        delete node;
    }
}

// 析构
HuffmanEncoder::~HuffmanEncoder() {
    freeNode(root_);
}

/// 如果是未压缩的，则进行压缩，如果是压缩，则解压缩
void HuffmanEncoder::run() {
    FILE* input_fp = nullptr;
    if ((input_fp = fopen(input_filename_.c_str(), "rb")) == nullptr) {
        printf("open file %s failed!\n", input_filename_.c_str());
        std::terminate();
    }

    std::array<char, kLenOfZipName> zip_name{};
    fread(zip_name.data(), kLenOfZipName, 1, input_fp);

    if (is_compressed_ || std::string_view{zip_name.data()} == kZipName) {
        // 无识别符，非压缩文件
        fclose(input_fp);
        printf("开始压缩文件%s......\n", input_filename_.c_str());
        printf("正在统计频率......\n");
        statisticalFrequency();
        printf("正在构建哈夫曼树......\n");
        root_ = buildHuffmanTree();
        printf("正在产生新编码......\n");
        initCodePoint(root_, 0, std::string(), 0);
        printHuffmanEncodeInfo();
        printf("正在压缩......\n");
        compress();  // 压缩
        printInfo("压缩");
        printf("压缩成功\n");

    } else {
        // 有识别符，是压缩文件
        printf("开始解压缩文件%s......\n", input_filename_.c_str());
        printf("读取原始文件信息......\n");
        char output_filename[kLenOfFileName];
        // 读入原文件名
        fread(output_filename, kLenOfFileName, 1, input_fp);
        printf("原始文件名为%s\n", output_filename);
        output_filename_ = std::string(output_filename);
        // 读入原文件大小
        fread(&output_filesize_, kLenOfFileSize, 1, input_fp);
        // 读入字符频率表
        for (int i = 0; i < kCodeNum; ++i) {
            fread(&points_.at(i).frequency, kLenOfCodeFrequency, 1, input_fp);
        }

        fclose(input_fp);
        printf("正在构建哈夫曼树......\n");
        root_ = buildHuffmanTree();
        printf("正在产生新编码......\n");
        initCodePoint(root_, 0, std::string(), 0);
        printHuffmanEncodeInfo();
        printf("正在解压缩......\n");
        uncompress();  // 解压缩
        printInfo("解压缩");
        printf("解压成功\n");
    }
}

// 统计频率
void HuffmanEncoder::statisticalFrequency() {
    // 打开文件
    FILE* input_fp = nullptr;
    if ((input_fp = fopen(input_filename_.c_str(), "rb")) == nullptr) {
        printf("open file %s failed!\n", input_filename_.c_str());
        std::terminate();
    }
    // 统计频率
    while (!feof(input_fp)) {
        Byte input_byte = 0;
        fread(&input_byte, 1, 1, input_fp);
        if (feof(input_fp)) {
            break;
        }
        ++points_[input_byte].frequency;
        ++input_filesize_;
    }
    fclose(input_fp);
}

// 构建 Huffman 树
auto HuffmanEncoder::buildHuffmanTree() -> HuffmanTreeNode* {
    // 使用优先队列，自定义比较器
    std::priority_queue<HuffmanTreeNode*, std::vector<HuffmanTreeNode*>, CmparatorOfHuffmanTreeNode>
        pq;
    // 初始化叶子节点
    for (int i = 0; i < kCodeNum; ++i) {
        pq.push(new HuffmanTreeNode(points_.at(i).frequency, &points_.at(i)));
    }
    // 取最小两个节点合并
    while (true) {
        HuffmanTreeNode* node1 = pq.top();
        pq.pop();
        HuffmanTreeNode* node2 = pq.top();
        pq.pop();
        auto* node3 = new HuffmanTreeNode(node1->weight + node2->weight, nullptr, node1, node2);
        // 优先队列为空则成功构建 Huffman 树，返回
        if (pq.empty()) {
            return node3;
        }
        pq.push(node3);
    }
}

// 获得 Huffman 编码
void HuffmanEncoder::initCodePoint(HuffmanTreeNode* node, CodeType new_code,
                                   const std::string& new_code_str, int length) {
    // 是叶子节点则结束，记录编码
    if (node->point != nullptr) {
        node->point->new_code = new_code;
        node->point->new_code_str = new_code_str;
        node->point->length = length;
        return;
    }
    // 否则向左右分支探索
    new_code <<= 1U;
    ++length;
    if (node->left != nullptr) {
        initCodePoint(node->left, new_code, new_code_str + "0", length);
    }
    if (node->right != nullptr) {
        initCodePoint(node->right, new_code + 1, new_code_str + "1", length);
    }
}

// 压缩文件
void HuffmanEncoder::compress() {
    auto free_file = [](FILE* file) { fclose(file); };
    std::unique_ptr<FILE, decltype(free_file)> input_fp{fopen(input_filename_.c_str(), "rb")};
    std::unique_ptr<FILE, decltype(free_file)> output_fp{fopen(output_filename_.c_str(), "wb")};

    if (input_fp == nullptr) {
        printf("open file %s failed!\n", input_filename_.c_str());
        std::terminate();
    }
    if (output_fp == nullptr) {
        printf("open file %s failed!\n", output_filename_.c_str());
        std::terminate();
    }

    // 写入压缩文件头信息
    // 识别符
    fwrite(kZipName.data(), kLenOfZipName, 1, output_fp.get());
    // 文件名
    fwrite(input_filename_.c_str(), kLenOfFileName, 1, output_fp.get());
    // 文件大小
    fwrite(&input_filesize_, kLenOfFileSize, 1, output_fp.get());
    // 字符频率，用以构建Huffman树
    for (int i = 0; i < kCodeNum; ++i) {
        fwrite(&points_.at(i).frequency, kLenOfCodeFrequency, 1, output_fp.get());
    }

    // 压缩所需临时辅助变量
    Byte input_byte = 0;
    Byte output_byte = 0;
    CodeType new_code = 0;
    int length = 0;
    int cnt = 0;
    CodeType cur_input_size = 0;
    CodeType cur_output_size = 0;
    double cur_rate = 0.0;

    while (feof(input_fp.get()) == 0) {
        // FIXME: 每次只读一个字节，可优化
        fread(&input_byte, 1, 1, input_fp.get());
        // 读到文件尾，结束
        if (feof(input_fp.get()) != 0) {
            break;
        }

        // 记录速率
        ++cur_input_size;
        double rate =
            static_cast<double>(cur_input_size) / static_cast<double>(input_filesize_) * 100.0;
        if (rate - cur_rate >= 10) {
            cur_rate = rate;
            printf("已压缩：%.1f%%\t压缩率: %.2f%%\n", cur_rate,
                   static_cast<double>(output_filesize_) / static_cast<double>(cur_input_size) *
                       100.0);
        }

        new_code = points_[static_cast<size_t>(input_byte)].new_code;
        length = points_[static_cast<size_t>(input_byte)].length;

        // 复用 output_byte 拼接 new_code 的 bit 流
        // +------------+-----------------+----+------------+
        // | new_code(1) | new_code(2)    | …… | new_code(n)|
        // +------------+-----------------+----+------------+
        // | 10011      | 111 1111 1111   | …… |            |
        // +-----------------+------------+----|------------+
        // |   outByte(1)    | outByte(2) | …… |  ……        |
        // +-----------------+------------+----+------------+
        while ((length--) != 0) {
            // 左移一位，最低位为新的可使用bit, 并将该bit置位 new_code
            // 需要写入的 bit
            output_byte <<= 1U;
            output_byte += (new_code >> static_cast<unsigned>(length)) & 1U;
            // 如果当前 output_byte 的 8bit 都已用完，写入文件，重新复用
            // FIXME: 1byte复用效率太低，也无缓存机制，可优化
            if (++cnt == 8) {
                fwrite(&output_byte, 1, 1, output_fp.get());
                output_byte = 0;
                cnt = 0;
                ++output_filesize_;
            }
        }
    }

    // 最后一个不足8比特填充 0
    if (cnt < 8) {
        assert(8 - cnt >= 0);
        output_byte <<= static_cast<unsigned>(8 - cnt);
        fwrite(&output_byte, 1, 1, output_fp.get());
        ++output_filesize_;
    }

    //  打印压缩信息
    printf("已压缩: %.1f%%\t压缩率: %.2f%%\n", 100.0,
           static_cast<double>(output_filesize_) / cur_input_size * 100);
}

// 搜索节点，辅助于解码
auto HuffmanEncoder::findNode(HuffmanTreeNode*& node, Byte input_byte, int& pos) -> bool {
    // 叶子节点搜索成功
    if (node->point != nullptr) {
        return true;
    }
    if (pos < 0) {
        return false;
    }
    int val = (input_byte >> static_cast<unsigned>(pos)) & 1U;
    --pos;
    if (val == 0) {
        node = node->left;
        return findNode(node, input_byte, pos);
    }
    node = node->right;
    return findNode(node, input_byte, pos);
}

// 解压缩
void HuffmanEncoder::uncompress() {
    FILE* input_fp = nullptr;
    if ((input_fp = fopen(input_filename_.c_str(), "rb")) == nullptr) {
        printf("open file %s failed!\n", input_filename_.c_str());
        std::terminate();
    }

    FILE* output_fp = nullptr;
    if ((output_fp = fopen(output_filename_.c_str(), "wb")) == nullptr) {
        printf("open file %s failed!\n", output_filename_.c_str());
        std::terminate();
    }

    fseek(input_fp, kLenOfZipHeader, SEEK_SET);
    input_filesize_ = kLenOfZipHeader;

    // 解压缩所需临时变量
    Byte input_byte = 0;
    Byte output_byte = 0;
    HuffmanTreeNode* node = root_;
    CodeType cur_output_size = 0;
    int pos = 0;
    double currRate = 0.0;

    while (feof(input_fp) == 0) {
        fread(&input_byte, 1, 1, input_fp);
        if (feof(input_fp) != 0) {
            break;
        }
        ++input_filesize_;
        pos = 7;

        while (findNode(node, input_byte, pos)) {
            output_byte = node->point->old_code;

            fwrite(&output_byte, 1, 1, output_fp);

            // 记录速率
            double rate = static_cast<double>(++cur_output_size) / output_filesize_ * 100;
            if (rate - currRate >= 10) {
                currRate = rate;
                //	system("cls");
                printf("已解压缩：%.1f%%\t解压缩率：%.2f%%\n", currRate,
                       static_cast<double>(cur_output_size) / input_filesize_ * 100);
            }

            if (cur_output_size == output_filesize_) {
                printf("已解压缩：%.1f%%\t解压缩率：%.2f%%\n", 100.0,
                       static_cast<double>(cur_output_size) / input_filesize_ * 100);
                break;
            }

            node = root_;
        }
    }
    fclose(input_fp);
    fclose(output_fp);
}

// 打印哈夫曼编码信息
void HuffmanEncoder::printHuffmanEncodeInfo() {
    printf("%-10s %-10s %-20s %-5s %-10s\n", "原码", "频率", "哈夫曼编码", "长度", "十进制");

    for (int i = 0; i < kCodeNum; ++i) {
        HuffmanCodePoint& code = points_.at(i);
        printf("%-10d %-10lu %-20s %-5d %-10lu\n", static_cast<int>(code.old_code), code.frequency,
               code.new_code_str.c_str(), code.length, code.new_code);
    }
}

// 打印压缩或解压缩信息
void HuffmanEncoder::printInfo(const char* type) const {
    double compress_rate = static_cast<double>(output_filesize_) / input_filesize_ * 100;
    printf("%s率: %.2f%%\n", type, compress_rate);
    auto input_filesize = static_cast<double>(input_filesize_);
    auto output_filesize = static_cast<double>(output_filesize_);
    if (input_filesize < 1024) {
        printf("输入文件大小：%.2fB, 输出文件大小：%.2fB\n", input_filesize, output_filesize);
        return;
    }

    input_filesize /= 1024;
    output_filesize /= 1024;
    if (input_filesize < 1024) {
        printf("输入文件大小：%.2fKB, 输出文件大小：%.2fKB\n", input_filesize, output_filesize);
        return;
    }

    input_filesize /= 1024;
    output_filesize /= 1024;
    if (input_filesize < 1024) {
        printf("输入文件大小：%.2fMB, 输出文件大小：%.2fMB\n", input_filesize, output_filesize);
        return;
    }

    input_filesize /= 1024;
    output_filesize /= 1024;
    if (input_filesize < 1024) {
        printf("输入文件大小：%.2fGB, 输出文件大小：%.2fGB\n", input_filesize, output_filesize);
        return;
    }
}

// 判断两个文件是否相等
auto HuffmanEncoder::equalFile(const std::string& filename1, const std::string& filename2) -> bool {
    FILE* fp1 = nullptr;
    if ((fp1 = fopen(filename1.c_str(), "rb")) == nullptr) {
        printf("open file %s failed!\n", filename1.c_str());
        std::terminate();
    }

    FILE* fp2 = nullptr;
    if ((fp2 = fopen(filename2.c_str(), "rb")) == nullptr) {
        printf("open file %s failed!\n", filename2.c_str());
        std::terminate();
    }

    while ((feof(fp1) == 0) && (feof(fp2) == 0)) {
        Byte uch1 = 0;
        Byte uch2 = 0;

        fread(&uch1, 1, 1, fp1);
        fread(&uch2, 1, 1, fp2);

        if (uch1 != uch2) {
            return false;
        }
    }

    return (feof(fp1) != 0) && (feof(fp2) != 0);
}

}  // namespace zfish