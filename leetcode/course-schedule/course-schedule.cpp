#include <cstdio>
#include <vector>

using std::vector;

void printVector(const char* p, const vector<int>& v)
{
    printf("%s: ", p);
    for (int i = 0; i < int(v.size()) - 1; i++) {
        printf("%d, ", i);
    }
    if (v.size() > 0) {
        printf("%d", v[v.size() - 1]);
    }
    printf("\n");
}

class Solution {
public:
    bool canFinish(int numCourses, vector<vector<int>>& prerequisites)
    {
        vector<Course> courses(numCourses);
        for (const auto& p : prerequisites) {
            ui16 next = p[0];
            ui16 prev = p[1];
            courses[next].prevCourses++;
            courses[prev].nextCourses.push_back(next);
        }

        vector<ui16> readyCourses;
        readyCourses.reserve(numCourses);
        for (ui16 courseIdx = 0; courseIdx < ui16(numCourses); courseIdx++) {
            if (courses[courseIdx].prevCourses == 0) {
                readyCourses.push_back(courseIdx);
            }
        }

        vector<ui16> nextReadyCourses;
        nextReadyCourses.reserve(numCourses);
        int remainingCourses = numCourses;
        while (readyCourses.size() > 0) {
            for (ui16 courseIdx : readyCourses) {
                Course& course = courses[courseIdx];
                for (int nextCourseIdx : course.nextCourses) {
                    Course& nextCourse = courses[nextCourseIdx];
                    nextCourse.prevCourses--;
                    if (nextCourse.prevCourses == 0) {
                        nextReadyCourses.push_back(nextCourseIdx);
                    }
                }
            }
            remainingCourses -= readyCourses.size();

            readyCourses = nextReadyCourses;
            nextReadyCourses.clear();
        }
        return remainingCourses == 0;
    }

private:
    using ui16 = unsigned short;

    struct Course {
        int prevCourses;
        vector<ui16> nextCourses;
    };
};

int main()
{
    int numCourses = 2;
    vector<vector<int>> prerequisites = {{1, 0}};
    printf("Answer: %d\n", Solution().canFinish(numCourses, prerequisites));
}
