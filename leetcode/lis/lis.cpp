#include <cstdio>
#include <map>
#include <vector>

using namespace std;

class Solution {
public:
    int lengthOfLIS(vector<int>& nums)
    {
        AVLTree curLengths;

        for (int v : nums) {
            int maxLength = curLengths.getMaxValueBelowKey(v);
            curLengths.put(v, maxLength + 1);
        }

        return curLengths.getMaxValue();
    }

private:
    // An AVL tree implementation, shamelessly copied from geeksforgeeks.com and augmented with
    // value and maxValue (maximum value in a subtree);
    class AVLTree {
    public:
        AVLTree()
            : root(nullptr)
        {
        }

        void put(int key, int value)
        {
            root = putImpl(root, key, value);
        }

        int getMaxValueBelowKey(int key) const
        {
            return getMaxValueBelowKeyImpl(root, key);
        }

        int getMaxValue() const
        {
            if (root == nullptr) {
                return 0;
            }
            return root->maxValue;
        }

    private:
        class Node {
        public:
            int key;
            int value;
            // Maximum value in the subtree.
            int maxValue;
            Node* left;
            Node* right;
            int height;
        };

        Node* root;

        static int height(Node* n)
        {
            return n == nullptr ? 0 : n->height;
        }

        static Node* newNode(int key, int value)
        {
            Node* node = new Node();
            node->key = key;
            node->value = value;
            node->maxValue = value;
            node->left = nullptr;
            node->right = nullptr;
            node->height = 1;
            return node;
        }

        static void updateMaxValue(Node* n)
        {
            n->maxValue = n->value;
            if (n->left != nullptr && n->left->maxValue > n->maxValue) {
                n->maxValue = n->left->maxValue;
            }
            if (n->right != nullptr && n->right->maxValue > n->maxValue) {
                n->maxValue = n->right->maxValue;
            }
        }

        static Node* rightRotate(Node* y)
        {
            Node* x = y->left;
            Node* t2 = x->right;

            x->right = y;
            y->left = t2;

            y->height = max(height(y->left), height(y->right)) + 1;
            x->height = max(height(x->left), height(x->right)) + 1;

            updateMaxValue(y);
            updateMaxValue(x);

            return x;
        }

        static Node* leftRotate(Node* x)
        {
            Node* y = x->right;
            Node* t2 = y->left;

            y->left = x;
            x->right = t2;

            x->height = max(height(x->left), height(x->right)) + 1;
            y->height = max(height(y->left), height(y->right)) + 1;

            updateMaxValue(x);
            updateMaxValue(y);

            return y;
        }

        static int getBalance(Node* n)
        {
            if (n == nullptr)
                return 0;
            return height(n->left) - height(n->right);
        }

        static Node* putImpl(Node* node, int key, int value)
        {
            if (node == nullptr)
                return newNode(key, value);

            if (key < node->key) {
                node->left = putImpl(node->left, key, value);
                if (node->left->maxValue > node->maxValue) {
                    node->maxValue = node->left->maxValue;
                }
            } else if (key > node->key) {
                node->right = putImpl(node->right, key, value);
                if (node->right->maxValue > node->maxValue) {
                    node->maxValue = node->right->maxValue;
                }
            } else {
                node->value = value;
                if (node->maxValue < value) {
                    node->maxValue = value;
                }
                return node;
            }

            node->height = 1 + max(height(node->left), height(node->right));

            int balance = getBalance(node);

            if (balance > 1 && key < node->left->key)
                return rightRotate(node);

            if (balance < -1 && key > node->right->key)
                return leftRotate(node);

            if (balance > 1 && key > node->left->key) {
                node->left = leftRotate(node->left);
                return rightRotate(node);
            }

            if (balance < -1 && key < node->right->key) {
                node->right = rightRotate(node->right);
                return leftRotate(node);
            }

            return node;
        }

        static int getMaxValueBelowKeyImpl(const Node* n, int key)
        {
            if (n == nullptr) {
                return 0;
            }

            if (n->key >= key) {
                return getMaxValueBelowKeyImpl(n->left, key);
            } else {
                int ret = n->value;
                if (n->left != nullptr) {
                    ret = max(ret, n->left->maxValue);
                }
                ret = max(ret, getMaxValueBelowKeyImpl(n->right, key));
                return ret;
            }
        }
    };
};

int main()
{
    vector<int> v1 = {10, 9, 2, 5, 3, 7, 101, 18};
    printf("v1 %d\n", Solution().lengthOfLIS(v1));
    vector<int> v2 = {3, 5, 6, 2, 5, 4, 19, 5, 6, 7, 12};
    printf("v2 %d\n", Solution().lengthOfLIS(v2));
    vector<int> v3 = { 10, 22, 9, 33, 21, 50, 41, 60, 80 };
    printf("v3 %d\n", Solution().lengthOfLIS(v3));
}
