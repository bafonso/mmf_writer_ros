/* 
 * File:   BackgroundRemovedImage.cpp
 * Author: Marc Gershow
 * 
 * Created on October 4, 2010, 9:17 AM
 *
 * (C) Marc Gershow; licensed under the Creative Commons Attribution Share Alike 3.0 United States License.
 * To view a copy of this license, visit http://creativecommons.org/licenses/by-sa/3.0/us/ or send a letter to
 * Creative Commons, 171 Second Street, Suite 300, San Francisco, California, 94105, USA.
 *
 */

#include "mmf_writer/BackgroundRemovedImage.h"
#include "mmf_writer/ImageMetaDataLoader.h"
#include "cv.h"
#include "cxcore.h"
//#include "cv"
//#include "cvtypes.h"
#include <vector>
#include <iostream>
#include <fstream>
#include <map>
#include <sstream>
#include <algorithm>

using namespace std;

static void writeImageData (ofstream &os, IplImage *im);
static IplImage *readImageData (ifstream &is, int width, int height, int depth, int nChannels);
//static ofstream logkludge("c:\\brilogstream.txt");
BackgroundRemovedImage::BackgroundRemovedImage() {
    init();
}

BackgroundRemovedImage::BackgroundRemovedImage(const BackgroundRemovedImage& orig) {
}

BackgroundRemovedImage::~BackgroundRemovedImage() {
    vector<pair<CvRect, IplImage *> >::iterator it;
    for (it = differencesFromBackground.begin(); it != differencesFromBackground.end(); ++it) {
        cvReleaseImage(&(it->second));
    }
    if (ms != NULL) {
        cvReleaseMemStorage(&ms);
    }
    if (metadata != NULL) {
        delete metadata;
    }
}

BackgroundRemovedImage::BackgroundRemovedImage(IplImage* src, const IplImage* bak, int threshBelowBackground, int threshAboveBackground, int smallDimMinSize, int lgDimMinSize, ImageMetaData* metadata) {
    init();
    //source and background must both be single channel arrays
    assert (src != NULL);
    assert (bak != NULL);
    assert (src->width == bak->width);
    assert (src->height == bak->height);
    assert (src->depth == bak->depth);
    assert (src->nChannels == 1);
    assert (bak->nChannels == 1);

    

    //update later, but for now just set memstorage to null, so it will be allocated on need
    ms = NULL;
    this->metadata = metadata;
    backgroundIm = bak;
    this->threshAboveBackground = threshAboveBackground;
    this->threshBelowBackground = threshBelowBackground;
    this->lgDimMinSize = lgDimMinSize;
    this->smallDimMinSize = smallDimMinSize;
    // logkludge << "entered extract differences" << endl<< flush;
    extractDifferences(src);
    // logkludge << "exited extrac differences" << endl<<flush;
}

void BackgroundRemovedImage::init() {
    ms = NULL;
    backgroundIm = NULL;
    metadata = NULL;
    threshAboveBackground = threshBelowBackground = 0;
    smallDimMinSize = 2;
    lgDimMinSize = 3;
    imOrigin.x = imOrigin.y = 0;
}


void BackgroundRemovedImage::extractDifferences(IplImage* src) {
  //  bool bwfreewhendone = (bwbuffer == NULL || bwbuffer->width != src->width || bwbuffer->height != src->height || bwbuffer->depth != IPL_DEPTH_8U);
    IplImage *bwbuffer = NULL, *srcbuffer1 = NULL, *srcbuffer2 = NULL;
    //allocate storage if necessary
    bwbuffer = cvCreateImage(cvGetSize(src), IPL_DEPTH_8U, 1);
    srcbuffer1 = cvCreateImage(cvGetSize(src), src->depth, 1);
    srcbuffer2 = cvCreateImage(cvGetSize(src), src->depth, 1);
    
    if (src->roi != NULL) {
        //logkludge << "src->roi = x,y: " << src->roi->xOffset << "," << src->roi->yOffset << "  w,h: " << src->roi->width << "," << src->roi->height << endl;
        cvSetImageROI(bwbuffer, cvGetImageROI(src));
        if (srcbuffer1 != NULL) {
             cvSetImageROI(srcbuffer1, cvGetImageROI(src));
        }
        if (srcbuffer2 != NULL) {
             cvSetImageROI(srcbuffer2, cvGetImageROI(src));
        }
    }

    // logkludge << "test point 1 in extract differences" << endl << flush;
    //compare image to background and find differences

    //logkludge << "tab, tbb = " << threshAboveBackground << ", " << threshBelowBackground;
    if (threshAboveBackground <= 0 && threshBelowBackground <= 0) {
        cvCmp (src, backgroundIm, bwbuffer, CV_CMP_NE);
    } else {
        //turn < bak into < bak + 1 which approximates <= bax
        threshAboveBackground = threshAboveBackground < 0 ? 0 : threshAboveBackground;
        threshBelowBackground = threshBelowBackground < 0 ? 0 : threshBelowBackground;

        //double bakmin, bakmax, srcmin, srcmax, b1min, b1max, b2min, b2max;

        cvAddS (backgroundIm, cvScalarAll(threshAboveBackground + 1), srcbuffer2);
        cvSubS (backgroundIm, cvScalarAll(threshBelowBackground), srcbuffer1);
        
//        cvMinMaxLoc(backgroundIm, &bakmin, &bakmax, NULL, NULL, NULL);
  //      cvMinMaxLoc(src, &srcmin, &srcmax, NULL, NULL, NULL);
    //    cvMinMaxLoc(srcbuffer1, &b1min, &b1max, NULL, NULL, NULL);
      //  cvMinMaxLoc(srcbuffer2, &b2min, &b2max, NULL, NULL, NULL);

        //logkludge << "src, bak, lowerbound, upperbound minima " << srcmin << ", " << bakmin << ", " << b1min << ", " << b2min << endl;
        //logkludge << "src, bak, lowerbound, upperbound maxima " << srcmax << ", " << bakmax << ", " << b1max << ", " << b2max << endl;
        

        cvInRange(src, srcbuffer1, srcbuffer2, bwbuffer);
        //logkludge << cvCountNonZero(bwbuffer) << "zero pixels" <<endl;
        cvNot(bwbuffer, bwbuffer);
    }
    //logkludge << cvCountNonZero(bwbuffer) << "nonzero pixels" <<endl;

    // logkludge <<  "test point 2 in extract differences" << endl << flush;
    //turn those differences into mini images
    // This is what takes most of the CPU TIME!
    extractBlobs (src, bwbuffer);

     // logkludge <<  "test point 3 in extract differences" << endl << flush;


    cvReleaseImage(&bwbuffer);
    cvReleaseImage(&srcbuffer1);
    cvReleaseImage(&srcbuffer2);
    
}
// Be careful about porting it to opencv > 1, see http://code.opencv.org/issues/1424
// STL arrays make opencv > 1 get slower..
// GPU does not speed it up: http://answers.opencv.org/question/84942/isnt-there-a-opencv-cuda-function-similar-to-findcontours/
void BackgroundRemovedImage::extractBlobs(IplImage *src, IplImage *mask) {
    bool freems = (ms == NULL);
    if (freems) {
        ms = cvCreateMemStorage(src->width*src->depth); //big block to save having to reallocate later
    }
    //logkludge << "created memstorage at " << (unsigned long long) ms << endl;
    CvSeq *contour;
    cvSetImageROI(mask, cvGetImageROI(src));
    cvFindContours(mask, ms, &contour, sizeof(CvContour), CV_RETR_EXTERNAL, CV_CHAIN_APPROX_NONE, cvPoint(0,0));
    CvPoint offset = (mask->roi == NULL) ? cvPoint(0,0) : cvPoint(mask->roi->xOffset, mask->roi->yOffset);
    //logkludge << "contour = " << (unsigned long long) contour << endl;

    IplImage *copy;
    CvRect roi = cvGetImageROI(src);
    if (contour != NULL) {
        CvRect r = cvBoundingRect(contour, 0);
        
       // subIm = cvCreateImageHeader(cvSize(r.width, r.height), src->depth, src->nChannels);
        for ( ; contour != NULL; contour = contour->h_next) {
            r = cvBoundingRect(contour, 0);
            if (r.width >= smallDimMinSize && r.height >= smallDimMinSize && (r.width >= lgDimMinSize || r.height >= lgDimMinSize)) {
                cvSetImageROI(src, r);
                copy = cvCreateImage(cvSize(r.width, r.height), src->depth, src->nChannels);
                cvCopy(src, copy);
                r.x += offset.x;
                r.y += offset.y;
                if (copy != NULL) {
                        differencesFromBackground.push_back(pair<CvRect, IplImage *> (r, copy));
                } else {
                    cout << "NULL copy -- wtf? r.x,y,w,h = " << r.x << "," << r.y << " ; " << r.width << "," << r.height << endl;
                }
            }
        }
    }
    if (freems) {
        cvReleaseMemStorage(&ms);
        ms = NULL;
    }
    cvSetImageROI(src, roi);
}

void BackgroundRemovedImage::toDisk(std::ofstream &os) {
    writeHeader(os);
    vector<pair<CvRect, IplImage *> >::iterator it;
    for (it = differencesFromBackground.begin(); it != differencesFromBackground.end(); ++it) {
        os.write((char *) &(it->first), sizeof(CvRect));
        writeImageData(os, it->second);
    }
}

void BackgroundRemovedImage::writeHeader(std::ofstream& os) {

    HeaderInfoT hi;

    hi.headersize = headerSizeInBytes;
    hi.idcode = idCode();


    //fill in header with 0s
    std::ofstream::pos_type cur_loc = os.tellp();
    char zero[headerSizeInBytes] = {0};
    os.write(zero, headerSizeInBytes);
    std::ofstream::pos_type end_loc = os.tellp();

    //return to the beginning of the header and write data
    os.seekp(cur_loc);

    if (differencesFromBackground.empty()) {
        hi.depth = 0;
        hi.nchannels = 0;
        hi.numims = 0;
    } else {
        hi.depth = differencesFromBackground.front().second->depth;
        hi.nchannels = differencesFromBackground.front().second->nChannels;
        hi.numims = differencesFromBackground.size();
    }
    os.write((char *) &hi, sizeof(hi));
    if (metadata != NULL) {
        metadata->toDisk(os);
    }
    os.seekp(end_loc);
    
}

int32_t BackgroundRemovedImage::sizeOnDisk() {
    int totalbytes = headerSizeInBytes;
    for (vector<pair<CvRect, IplImage *> >::iterator it = differencesFromBackground.begin(); it != differencesFromBackground.end(); ++it) {
        IplImage *im = it->second;
        totalbytes += sizeof(CvRect) + bytesPerPixel(im)*im->width*im->height*im->nChannels;
    }
    return totalbytes;
}

//does not include background image or memstorage
int BackgroundRemovedImage::sizeInMemory() {
    int totalbytes = sizeof(this);
    for (vector<pair<CvRect, IplImage *> >::iterator it = differencesFromBackground.begin(); it != differencesFromBackground.end(); ++it) {
        IplImage *im = it->second;
        totalbytes += sizeof(CvRect) + sizeof(IplImage) + im->imageSize;
    }
    return totalbytes;
}
void writeImageData (ofstream &os, IplImage *im) {
    //if the width and widthstep are the same, we can just dump all the data
    if (im->width*im->nChannels*BackgroundRemovedImage::bytesPerPixel(im) == im->widthStep) {
        os.write(im->imageData, im->imageSize);
    } else {
        //otherwise copy the image data to a contiguous block of memory, then dump
        int bytes_per_row = im->width*im->nChannels*BackgroundRemovedImage::bytesPerPixel(im);
        char *buffer = (char *) malloc(bytes_per_row * im->height);
        for (int row = 0; row < im->height; ++row) {
            memcpy(buffer + row * bytes_per_row, im->imageDataOrigin + row*im->widthStep, bytes_per_row);
        }
        os.write(buffer, im->height * bytes_per_row);
        free(buffer);
    }
}

IplImage *readImageData (ifstream &is, int width, int height, int depth, int nChannels) {
    IplImage *im = cvCreateImage(cvSize(width, height), depth, nChannels);
    assert (im != NULL);
    if (im->width*im->nChannels*BackgroundRemovedImage::bytesPerPixel(im) == im->widthStep) {
        is.read(im->imageData, im->imageSize);
    } else {
         //otherwise copy the image data to a contiguous block of memory, then dump
        int bytes_per_row = im->width*im->nChannels*BackgroundRemovedImage::bytesPerPixel(im);
        char *buffer = (char *) malloc(bytes_per_row * im->height);
        is.read(buffer, im->height * bytes_per_row);
        for (int row = 0; row < im->height; ++row) {
            memcpy(im->imageDataOrigin + row*im->widthStep, buffer + row * bytes_per_row, bytes_per_row);
        }
        
        free(buffer);
    }
    return im;
    
}

BackgroundRemovedImage *BackgroundRemovedImage::fromDisk(std::ifstream& is, const IplImage *bak) {
    BackgroundRemovedImage *bri = new BackgroundRemovedImage();
    bri->backgroundIm = bak;
    bri->threshAboveBackground = bri->threshBelowBackground = 0;
    bri->ms = NULL;
   
    std::ifstream::pos_type cur_loc = is.tellg();

    HeaderInfoT hi;
    is.read((char *) &hi, sizeof(hi));
    if (hi.idcode != bri->idCode()) {
        cout << "WARNING: id code does not match when loading BackgroundRemovedImage";
    }
    bri->metadata = ImageMetaDataLoader::fromFile(is);
    is.seekg(cur_loc + (std::ifstream::pos_type) hi.headersize);
 //   cout << "depth= " << hi.depth << ";  numims= " << hi.numims << ";  nchannels= " << hi.nchannels << endl;
    for (int j = 0; j < hi.numims; ++j) {
        CvRect r; //BUG CHECK: possible problem if int is not 32 bits on your system
        assert (sizeof(CvRect) == 16);
        is.read((char *) &r, sizeof(CvRect));
   //     cout << "j: " << j << "  w: " << r.width << "  h: " << r.height << "\t";
        IplImage *im = readImageData(is, r.width, r.height, hi.depth, hi.nchannels);
        bri->differencesFromBackground.push_back(pair<CvRect, IplImage *> (r, im));
    }
  //  cout << endl;
    return bri;
}

void BackgroundRemovedImage::restoreImage(IplImage** dst) {
    if (backgroundIm == NULL) {
        return;
    }

    setImOriginFromMetaData();
    IplImage *tmp = NULL;
    assert(dst != NULL);
    /* if there is an image offset, we restore the image as normal, but then copy
       into a larger blank image, so first we store the destination in a temporary spot
       and create a new blank image
     */
    //logkludge << "imOrigin = " << imOrigin.x << " , " << imOrigin.y << endl;
    if (imOrigin.x != 0 || imOrigin.y != 0) {
        tmp = *dst;
        *dst = NULL;
      //  cout << "imorigin.x = " << imOrigin.x << "imorigin.y = " << imOrigin.y;
    }
    
    if (*dst == NULL || (*dst)->width != backgroundIm->width ||  (*dst)->height != backgroundIm->height ||
             (*dst)->nChannels != backgroundIm->nChannels ||  (*dst)->depth != backgroundIm->depth) {
        if (*dst != NULL) {
            cvReleaseImage(dst);
        }
        //logkludge << "calling cvCloneImage on backgroundIm" << endl;
        *dst = cvCloneImage(backgroundIm);
    } else {
        //logkludge << "calling cvCopyImage on backgroundIm" << endl;
        cvCopyImage(backgroundIm, *dst);
    }
    CvRect roi = cvGetImageROI(*dst);
    vector< pair<CvRect, IplImage *> >::iterator it;
    //logkludge << "num rectangles = " << differencesFromBackground.size() << endl;
    for (it = differencesFromBackground.begin(); it != differencesFromBackground.end(); ++it) {
        //logkludge << "roi = x,y,w,h = " << it->first.x << ", " << it->first.y << ", " << it->first.width << ", " << it->first.height << endl;
        //logkludge << "imsize = (w,h) " << it->second->width << ", " << it->second->height << endl;
        cvSetImageROI(*dst, it->first);
        cvCopyImage(it->second, *dst);
    }
    cvSetImageROI(*dst, roi);

    /* If there is an offset, we create storage if necessary
     * (remember tmp points to *dst)
     * then zero the image, and copy the bri to the correct place, then delete
     * the untranslated image
     */
    if (imOrigin.x != 0 || imOrigin.y != 0) {
        if (tmp == NULL || (tmp)->width != (backgroundIm->width + imOrigin.x) ||  (tmp)->height != (backgroundIm->height + imOrigin.y) ||
             (tmp)->nChannels != backgroundIm->nChannels ||  (tmp)->depth != backgroundIm->depth) {
            if (tmp != NULL) {
                cvReleaseImage(&tmp);
            }
            tmp = cvCreateImage(cvSize(backgroundIm->width + imOrigin.x, backgroundIm->height + imOrigin.y), backgroundIm->depth, backgroundIm->nChannels);
        }
        cvSetZero(tmp);
        CvRect r;
        r.x = imOrigin.x; r.y = imOrigin.y; r.width = backgroundIm->width; r.height = backgroundIm->height;
        cvSetImageROI(tmp, r);
        cvCopyImage(*dst, tmp);
        cvReleaseImage(dst);
        *dst = tmp;
    }
    //logkludge << "finished restoring image " << endl;
}

void BackgroundRemovedImage::annotateImage(IplImage* dst, CvScalar color, int thickness) {
    assert(dst != NULL);
    setImOriginFromMetaData();
    for (vector< pair<CvRect, IplImage *> >::iterator it = differencesFromBackground.begin(); it != differencesFromBackground.end(); ++it) {
        CvRect r = it->first;
        cvRectangle(dst, cvPoint(r.x+imOrigin.x, r.y+imOrigin.y), cvPoint(r.x + r.width + imOrigin.x, r.y + r.height + imOrigin.y), color, thickness, 8, 0);
    }
}

CvPoint BackgroundRemovedImage::getImageOrigin() {
    return imOrigin;
}

std::string BackgroundRemovedImage::saveDescription() {
 //   cout << "entered bri save description\n";
    std::stringstream os;
    os << classname() << ": header is a" << headerDescription() << "header is followed by numims image blocks of the following form:\n";
    os << "(" << sizeof(CvRect) << " bytes) CvRect [x y w h] describing location of image data, then interlaced row ordered image data\n";
    return os.str();
 //   cout << "ended bri save description\n";
}

std::string BackgroundRemovedImage::headerDescription() {
    std::stringstream os;
    os << headerSizeInBytes << " byte zero padded header with the following data fields (all " << sizeof(int) << " byte ints, except id code)\n";
    os << sizeof(uint32_t) << " byte uint32_t idcode = " << hex << idCode() << dec << "headersize (number of bytes in header), depth (IplImage depth), nChannels (IplImage number of channels), numims (number of image blocks that differ from background) then metadata:\n";
    if (metadata != NULL) {
 //       cout << metadata->saveDescription();
        os << metadata->saveDescription();
    } else {
        os << "no metadata\n";
    }
    return os.str();
}

int BackgroundRemovedImage::numRegions() const {
    return differencesFromBackground.size();
}

template<class subclass, class superclass> bool BackgroundRemovedImage::isa (const superclass *obj, const subclass * &dst) {
    dst = dynamic_cast<const subclass *> (obj);
    return (dst != NULL);
}
template<class subclass, class superclass> bool BackgroundRemovedImage::isa (superclass *obj, subclass * &dst) {
    dst = dynamic_cast<subclass *> (obj);
    return (dst != NULL);
}
template<class subclass, class superclass> bool BackgroundRemovedImage::isa (const superclass *obj) {
    const subclass *dst;
    return isa<subclass>(obj, dst);
}

void BackgroundRemovedImage::setImOriginFromMetaData() {
    if (metadata == NULL) {
        return;
    }
//    const MightexMetaData *md;
//    if (isa<MightexMetaData> (metadata, md)) {
//        TProcessedDataProperty tp = md->getAttributes();
//        imOrigin.x = tp.XStart;
//        imOrigin.y = tp.YStart;
//        return;
//    }
//    CompositeImageMetaData *cmd;
//    if (isa<CompositeImageMetaData>(metadata, cmd)) {
//        vector<const ImageMetaData *> v = cmd->getMetaDataVector();
//        for (vector<const ImageMetaData *>::const_iterator it = v.begin(); it != v.end(); ++it) {
//
//            if (isa<MightexMetaData> (*it, md)) {
//                TProcessedDataProperty tp = md->getAttributes();
//                imOrigin.x = tp.XStart;
//                imOrigin.y = tp.YStart;
//                return;
//            }
//        }
//    }
}

IplImage *BackgroundRemovedImage::subImageFromConst(const IplImage* src, CvRect& r) {
    if (src == NULL)
        return NULL;
    IplImage *dst = cvCreateImage(cvSize(r.width, r.height), src->depth, src->nChannels);
    r.x = r.x < 0 ? 0 : r.x;
    r.y = r.y < 0 ? 0 : r.y;
    r.x = (r.x + r.width) > src->width ? src->width - r.width  : r.x;
    r.y = (r.y + r.height) > src->height ? src->height - r.height : r.y;

    
    
  //  cvSetImageROI(src, r);

    char *srcPtr, *dstPtr;
    int pxsize = bytesPerPixel(src)*src->nChannels;
    for (int j = 0; j < r.height; ++j) {
        srcPtr = src->imageData + (r.y + j)*src->widthStep + (r.x * pxsize);
        dstPtr = dst->imageData + j*dst->widthStep;
        memcpy(dstPtr, srcPtr, r.width*pxsize);
    }
    return dst;

}

std::pair<CvRect,IplImage*> BackgroundRemovedImage::mergeBlobs(std::pair<CvRect,IplImage*> b1, std::pair<CvRect,IplImage*> b2, mergeFunctionT mergeFunction){
    CvRect r, r1, r2, roi;
    IplImage *im, *im1, *im2;
    r1 = b1.first;
    r2 = b2.first;
    im1 = b1.second;
    im2 = b2.second;
//    r.x = min(r1.x, r2.x);
//    r.width = max(r1.x + r1.width, r2.x + r2.width) - r.x;
//    r.y = min(r1.y, r2.y);
//    r.height = max(r1.y + r1.height, r2.y + r2.height) - r.y;
    r = cvMaxRect(&r1, &r2);
    if (backgroundIm == NULL) {
        im = cvCreateImage(cvSize(r.width, r.height), im1->depth, im1->nChannels);
        cvSetZero(im);
    } else {
        im = subImageFromConst(backgroundIm, r);
    }
    //cout << "reseting roi" << endl << flush;
    assert (im != NULL);
    assert (im1 != NULL);
    assert (im2 != NULL);
    cvResetImageROI(im1);
    cvResetImageROI(im2);
    
   // cout << "copying im 1" << endl << flush;
    roi.x = r1.x - r.x;
    roi.y = r1.y - r.y;
    roi.width = r1.width; 
    roi.height = r1.height;
   // cout << "im1 size = " << im1->width << " , " << im1->height << endl << flush;
   // cout << "im size = " << im->width << " , " << im->height << endl << flush;
  //  cout << "roi = " << roi.x << " , " << roi.y << "; " << roi.width << " , " << roi.height << endl << flush;
    cvSetImageROI(im, roi);
    cvCopy(im1, im, NULL);
    
  //   cout << "copying im 2" << endl << flush;
    roi.x = r2.x - r.x;
    roi.y = r2.y - r.y;
    roi.width = r2.width; 
    roi.height = r2.height;
    cvSetImageROI(im, roi);
    cvCopy(im2, im, NULL);
    
    if (mergeFunction != NULL) {
        CvRect ri, roi1, roi2;
        ri.x = max(r1.x, r2.x);
        ri.width = min(r1.x + r1.width, r2.x + r2.width) - ri.x;
        ri.y = max(r1.y, r2.y);
        ri.height = min(r1.y + r1.height, r2.y + r2.height) - ri.y;
        if (ri.width > 0 && ri.height > 0) {
            roi1 = roi2 = roi = ri;
            roi.x = ri.x - r.x;
            roi.y = ri.y - r.y;
           
            roi1.x = ri.x - r1.x;
            roi1.y = ri.y - r1.y;
            
            roi2.x = ri.x - r2.x;
            roi2.y = ri.y - r2.y;
            
            cvSetImageROI(im, roi);
            cvSetImageROI(im1, roi1);
            cvSetImageROI(im2, roi2);
            mergeFunction(im1, im2, im);
            assert (im != NULL && im1 != NULL && im2 != NULL);
            cvResetImageROI(im);   
            cvResetImageROI(im1);   
            cvResetImageROI(im2);   
        }
    }
    
    return pair<CvRect,IplImage*> (r, im);
}

static bool comparePairsByRectX (pair<CvRect, IplImage *>a, pair<CvRect, IplImage *> b) {
    return a.first.x < b.first.x;
}

void BackgroundRemovedImage::mergeRegions(const BackgroundRemovedImage* bri2, mergeFunctionT mergeFunction) {
    if (mergeFunction == NULL) {
        if (threshAboveBackground > 0 && threshBelowBackground <= 0) {
            //background is minimum, so take maximum
            mergeFunction = cvMax;
        } else if (threshAboveBackground <= 0 && threshBelowBackground > 0) {
            //background is maximum, so take minimum
            mergeFunction = cvMax;
        } else {
            mergeFunction = avgOfTwoIms;
        }
    }

    //cout << "merging two sets of differences" << endl;
    for (vector< pair<CvRect, IplImage *> >::const_iterator it = bri2->differencesFromBackground.begin(); it != bri2->differencesFromBackground.end(); ++ it) {
        IplImage *copy = cvCloneImage (it->second);
        assert (copy != NULL);
        differencesFromBackground.push_back(pair<CvRect, IplImage *>(it->first, copy));
    }
 //   differencesFromBackground.insert(differencesFromBackground.end(), bri2->differencesFromBackground.begin(), bri2->differencesFromBackground.end());
    
    //cout << "compacting blobs" << endl;
    while (compactBlobs(mergeFunction)) {
     //   cout << "calling merge function an additional time" << endl;
    };
    
}

bool BackgroundRemovedImage::compactBlobs(mergeFunctionT mergeFunction) {
    bool rv = false;
    //cout << "sorting" << endl << flush;
    std::sort (differencesFromBackground.begin(), differencesFromBackground.end(), comparePairsByRectX);
    vector< pair<CvRect, IplImage *> >::iterator it, it2;
    for (it = differencesFromBackground.begin(); it < differencesFromBackground.end(); ++it) {
        for (it2 = it + 1; it2 != differencesFromBackground.end() && it2->first.x < it->first.x + it->first.width; ) {
            if (rectanglesIntersect(it->first, it2->first)) {
                //cout << "merging two rectangles" << endl << flush;
                rv = true;
                pair<CvRect, IplImage *> newblob = mergeBlobs(*it, *it2, mergeFunction);
                //cout << "freeing memory" << endl << flush;
                cvReleaseImage(&(it2->second));
                cvReleaseImage(&(it->second));
                it2 = differencesFromBackground.erase(it2);
                *it = newblob;
                //cout << "rectangles merged" << endl << flush;
            } else {
                ++it2;
            }
        }
    }
    return rv;
}
