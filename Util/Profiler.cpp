#include <Util/Profiler.hpp>

#include <sstream>

using namespace std;

ProfilerSample  Profiler::mFrames[PROFILER_FRAME_COUNT];
ProfilerSample* Profiler::mCurrentSample = nullptr;
uint64_t Profiler::mCurrentFrame = 0;
const std::chrono::high_resolution_clock Profiler::mTimer;

void Profiler::BeginSample(const string& label, bool resume = false) {
	if (resume) {
		for (auto& c : mCurrentSample->mChildren)
			if (label == c.mLabel) {
				mCurrentSample = &c;
				c.mStartTime = mTimer.now();
				c.mCalls++;
				return;
			}
	}

	mCurrentSample->mChildren.push_back({});
	ProfilerSample* s = &mCurrentSample->mChildren.back();
	memset(s, 0, sizeof(ProfilerSample));
	
	strcpy(s->mLabel, label.c_str());
	s->mParent = mCurrentSample;
	s->mStartTime = mTimer.now();
	s->mCalls = 1;
	s->mChildren = {};

	mCurrentSample =  s;
}
void Profiler::EndSample() {
	if (!mCurrentSample->mParent) {
		fprintf_color(COLOR_RED, stderr, "Error: Attempt to end nonexistant Profiler sample!");
		throw;
	}
	mCurrentSample->mTime += mTimer.now() - mCurrentSample->mStartTime;
	mCurrentSample = mCurrentSample->mParent;
}

void Profiler::FrameStart() {
	int i = mCurrentFrame % PROFILER_FRAME_COUNT;
	sprintf(mFrames[i].mLabel, "Frame  %llu", mCurrentFrame);
	mFrames[i].mParent = nullptr;
	mFrames[i].mStartTime = mTimer.now();
	mFrames[i].mTime = chrono::nanoseconds::zero();
	mFrames[i].mChildren.clear();
	mCurrentSample = &mFrames[i];
}
void Profiler::FrameEnd() {
	int i = mCurrentFrame % PROFILER_FRAME_COUNT;
	mFrames[i].mTime += mTimer.now() - mFrames[i].mStartTime;
	mCurrentFrame++;
	mCurrentSample = nullptr;
}

void PrintSample(char*& data, const ProfilerSample* s, uint32_t tabLevel, double minTime) {
	double t = s->mTime.count() * 1e-6;
	for (uint32_t i = 0; i < tabLevel; i++)
		data += sprintf(data, "    ");
	if (t >= minTime) data += sprintf(data, "%s (%u): %.2fms\n", s->mLabel, s->mCalls, t);
	for (const auto& pc : s->mChildren)
		PrintSample(data, &pc, tabLevel + 1, minTime);
}
void Profiler::PrintLastFrame(char* buffer, double minTime) {
	PrintSample(buffer, &mFrames[(mCurrentFrame + PROFILER_FRAME_COUNT - 1) % PROFILER_FRAME_COUNT], 0, minTime);
}