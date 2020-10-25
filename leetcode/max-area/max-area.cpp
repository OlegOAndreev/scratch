#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>

using std::vector;

class Solution {
public:
    int maxAreaOfIsland(vector<vector<int>>& grid)
    {
        if (grid.empty()) {
            return 0;
        }

        size_t rowSize = grid[0].size();
        vector<int> previousRow(rowSize, -1);

        int numColors = 0;
        DisjointSet equivalentColors;
        vector<int> colorAreas;

        for (int r = 0; r < grid.size(); r++) {
            const vector<int>& row = grid[r];
            assert(row.size() == previousRow.size());
            int curColor = -1;
            for (int c = 0; c < row.size(); c++) {
                if (row[c] == 0) {
                    curColor = -1;
                } else {
                    if (curColor == -1) {
                        if (previousRow[c] != -1) {
                            curColor = previousRow[c];
                        } else {
                            curColor = numColors;
                            numColors++;
                            colorAreas.push_back(0);
                        }
                    } else {
                        if (previousRow[c] != -1) {
                            equivalentColors.addUnion(curColor, previousRow[c]);
                        }
                    }
                }
                previousRow[c] = curColor;
                if (curColor != -1) {
                    colorAreas[curColor]++;
                }
            }
        }

        int maxArea = 0;
        for (int color = 0; color < numColors; color++) {
            int rootColor = equivalentColors.findRoot(color);
            if (rootColor != color) {
                colorAreas[rootColor] += colorAreas[color];
            }
            if (colorAreas[rootColor] > maxArea) {
                maxArea = colorAreas[rootColor];
            }
        }
        return maxArea;
    }

private:
    class DisjointSet {
    public:
        void addUnion(int index1, int index2)
        {
            if (index1 == index2) {
                return;
            }
            resizeParents(std::max(index1, index2));

            int root1 = findRoot(index1);
            int root2 = findRoot(index2);
            if (root1 != root2) {
                if (rank[root1] > rank[root2]) {
                    parent[root2] = root1;
                } else if (rank[root1] < rank[root2]) {
                    parent[root1] = root2;
                } else {
                    parent[root1] = root2;
                    rank[root2]++;
                }
            }
        }

        int findRoot(int index)
        {
            if (parent.size() <= index || parent[index] == -1) {
                return index;
            }

            int p = index;
            while (parent[p] != -1) {
                p = parent[p];
            }
            parent[index] = p;
            return p;
        }

    private:
        // Maps from index to parent index, -1 means the index is the base index.
        vector<int> parent;
        vector<int> rank;

        void resizeParents(int maxIndex)
        {
            if (parent.size() <= maxIndex) {
                parent.resize(maxIndex + 1, -1);
                rank.resize(maxIndex + 1, 0);
            }
        }
    };
};

int main()
{
    // clang-format off
    vector<vector<int>> example1 = {
       {0,0,1,0,0,0,0,1,0,0,0,0,0},
       {0,0,0,0,0,0,0,1,1,1,0,0,0},
       {0,1,1,0,1,0,0,0,0,0,0,0,0},
       {0,1,0,0,1,1,0,0,1,0,1,0,0},
       {0,1,0,0,1,1,0,0,1,1,1,0,0},
       {0,0,0,0,0,0,0,0,0,0,1,0,0},
       {0,0,0,0,0,0,0,1,1,1,0,0,0},
       {0,0,0,0,0,0,0,1,1,0,0,0,0}
    };
    // clang-format on
    printf("Answer 1: %d\n", Solution().maxAreaOfIsland(example1));
    // clang-format off
    vector<vector<int>> example2 = {{0,0,0,0,0,0,0,0}};
    // clang-format on
    printf("Answer 2: %d\n", Solution().maxAreaOfIsland(example2));
}
