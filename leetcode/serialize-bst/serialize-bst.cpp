#include <cstdint>
#include <cstdio>
#include <string>

using std::string;

struct TreeNode {
    int val;
    TreeNode* left;
    TreeNode* right;
    TreeNode(int x)
        : val(x)
        , left(NULL)
        , right(NULL)
    {
    }
};

#define DO_DELTA_VALUES
#define DO_PREALLOCATE

class Codec {
public:
    static_assert(sizeof(int) < sizeof(uint64_t), "This is a strange platform");

    using uchar = unsigned char;

    size_t readVarInt(const char* b, uint64_t* value)
    {
        size_t v = 0;
        size_t pos = 0;
        int shift = 0;
        while (true) {
            uchar ub = b[pos];
            if (ub < 0x80) {
                v |= ub << shift;
                *value = v;
                return pos + 1;
            } else {
                v |= (ub & 0x7F) << shift;
                shift += 7;
                pos++;
            }
        }
    }

    size_t writeVarInt(char* b, uint64_t value)
    {
        size_t pos = 0;
        while (true) {
            if (value < 0x80) {
                b[pos] = value;
                return pos + 1;
            } else {
                b[pos] = (value & 0x7F) | 0x80;
                value >>= 7;
                pos++;
            }
        }
    }

    size_t varIntLen(uint64_t value)
    {
        size_t len = 0;
        while (true) {
            if (value < 0x80) {
                return len + 1;
            } else {
                value >>= 7;
                len++;
            }
        }
    }

#ifdef DO_DELTA_VALUES
    template<bool isLeft>
    size_t calcRequiredSize(TreeNode* node, int parentVal)
    {
        bool hasLeft = node->left != nullptr;
        bool hasRight = node->right != nullptr;
        // Make sure that deltaVal is not less than zero (given that the tree is BST).
        unsigned deltaVal = isLeft ? unsigned(parentVal) - unsigned(node->val)
                                   : unsigned(node->val) - unsigned(parentVal);
        uint64_t v = deltaVal << 2 | (hasLeft) << 1 | hasRight;
        size_t len = varIntLen(v);
        if (hasLeft) {
            len += calcRequiredSize<true>(node->left, node->val);
        }
        if (hasRight) {
            len += calcRequiredSize<false>(node->right, node->val);
        }
        return len;
    }
#else
    size_t calcRequiredSize(TreeNode* node)
    {
        bool hasLeft = node->left != nullptr;
        bool hasRight = node->right != nullptr;
        uint64_t v = uint64_t(node->val) << 2 | (hasLeft) << 1 | hasRight;
        size_t len = varIntLen(v);
        if (hasLeft) {
            len += calcRequiredSize(node->left);
        }
        if (hasRight) {
            len += calcRequiredSize(node->right);
        }
        return len;
    }
#endif

#ifdef DO_DELTA_VALUES
    template<bool isLeft>
    void serializeImpl(TreeNode* node, int parentVal, string& s)
    {
        bool hasLeft = node->left != nullptr;
        bool hasRight = node->right != nullptr;
        // Make sure that deltaVal is not less than zero (given that the tree is BST).
        unsigned deltaVal = isLeft ? unsigned(parentVal) - unsigned(node->val)
                                   : unsigned(node->val) - unsigned(parentVal);
        uint64_t v = deltaVal << 2 | (hasLeft) << 1 | hasRight;
        char buf[5];
        size_t len = writeVarInt(buf, v);
        s.append(buf, len);
        if (hasLeft) {
            serializeImpl<true>(node->left, node->val, s);
        }
        if (hasRight) {
            serializeImpl<false>(node->right, node->val, s);
        }
    }
#else
    void serializeImpl(TreeNode* node, string& s)
    {
        bool hasLeft = node->left != nullptr;
        bool hasRight = node->right != nullptr;
        uint64_t v = uint64_t(node->val) << 2 | (hasLeft) << 1 | hasRight;
        char buf[5];
        size_t len = writeVarInt(buf, v);
        s.append(buf, len);
        if (hasLeft) {
            serializeImpl(node->left, s);
        }
        if (hasRight) {
            serializeImpl(node->right, s);
        }
    }
#endif

    // Encodes a tree to a single string.
    string serialize(TreeNode* root)
    {
        string ret;
        if (root != nullptr) {
#ifdef DO_DELTA_VALUES
#ifdef DO_PREALLOCATE
            ret.reserve(calcRequiredSize<true>(root, 0));
#endif
            serializeImpl<true>(root, 0, ret);
#else
#ifdef DO_PREALLOCATE
            ret.reserve(calcRequiredSize(root));
#endif
            serializeImpl(root, ret);
#endif
        }
        return ret;
    }

#ifdef DO_DELTA_VALUES
    template<bool isLeft>
    TreeNode* deserializeImpl(int parentVal, const string& s, size_t& pos)
    {
        uint64_t v;
        size_t len = readVarInt(s.data() + pos, &v);
        pos += len;
        bool hasLeft = (v >> 1) & 1;
        bool hasRight = v & 1;
        unsigned deltaVal = v >> 2;
        int val = isLeft ? unsigned(parentVal) - deltaVal : unsigned(parentVal) + deltaVal;
        TreeNode* ret = new TreeNode(val);
        if (hasLeft) {
            ret->left = deserializeImpl<true>(val, s, pos);
        }
        if (hasRight) {
            ret->right = deserializeImpl<false>(val, s, pos);
        }
        return ret;
    }
#else
    TreeNode* deserializeImpl(const string& s, size_t& pos)
    {
        uint64_t v;
        size_t len = readVarInt(s.data() + pos, &v);
        pos += len;
        bool hasLeft = (v >> 1) & 1;
        bool hasRight = v & 1;
        int val = int(v >> 2);
        TreeNode* ret = new TreeNode(val);
        if (hasLeft) {
            ret->left = deserializeImpl(s, pos);
        }
        if (hasRight) {
            ret->right = deserializeImpl(s, pos);
        }
        return ret;
    }
#endif

    // Decodes your encoded data to tree.
    TreeNode* deserialize(string data)
    {
        if (data.empty()) {
            return nullptr;
        } else {
            size_t pos = 0;
#ifdef DO_DELTA_VALUES
            return deserializeImpl<true>(0, data, pos);
#else
            return deserializeImpl(data, pos);
#endif
        }
    }
};

int main()
{
    TreeNode* root = new TreeNode(1000);
    root->left = new TreeNode(100);
    root->left->left = new TreeNode(0);
    root->left->right = new TreeNode(900);
    root->right = new TreeNode(100000);
    root->right->left = new TreeNode(1010);
    root->right->right = new TreeNode(10000000);
    root->right->right->left = new TreeNode(1000000);
    root->right->right->right = new TreeNode(100000000);

        Codec codec;
    string s = codec.serialize(root);
    printf("Got string length %d, capacity %d\n", int(s.length()), int(s.capacity()));
    TreeNode* out = codec.deserialize(s);
    printf("Root %d, left %d, right %d\n", out->val, out->left->val, out->right->val);
}
