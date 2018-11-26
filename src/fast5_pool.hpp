/* MIT License
 *
 * Copyright (c) 2018 Sam Kovaka <skovaka@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef MAP_POOL_HPP
#define MAP_POOL_HPP

#include <thread>
#include <vector>
#include <deque>
#include "mapper.hpp"

class Fast5Pool {
    public:
    Fast5Pool(MapperParams &params, u16 nthreads, u32 batch_size);
    
    void add_fast5s(const std::vector<std::string> &new_fast5s);
    std::vector<std::string> update();
    bool all_finished();
    void stop_all();

    private:

    class MapperThread {
        public:
        MapperThread(MapperParams &params);
        MapperThread(MapperThread &&mt);

        void start();
        void run();

        bool running_, aligning_;
        Mapper mapper_;
        std::thread thread_;
        std::deque<std::string> ids_in_;
        std::deque< std::vector<float> > signals_in_;
        std::deque<ReadLoc> locs_out_;
        std::mutex in_mtx_, out_mtx_;
    };

    u16 nthreads_;
    std::vector<MapperThread> threads_;
    std::deque<std::string> fast5s_;
    u32 batch_size_;
};


#endif