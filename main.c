#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>
#include <time.h>

#define MAX_FNAME	64
#define FILEHEADER	14
#define INFOHEADER	40

int width = 0, height = 0, nThreads = 0, part = 0;
char fName[MAX_FNAME] = "screen.bmp";
char *outName = "copy.bmp";
int sobelMatrix[3][3] = {
	{ 1,	2,	1 },
	{ 0,	0,	0 },
	{ -1,	-2,	-1 }
};

typedef struct {
	int *sobelArr;
	int *pixArr;
	int i;
} Args;

//turning image to blackwhite colors
int turnToGrey(int x) {
	int res = (((x >> 16) & 255)+((x >> 8) & 255)+(x & 255)) / 3;
	return (res << 16) | (res << 8) | res;
}

//gradient for Y
int sumY(int* arr, int x, int y) {
	int res = 0;
	res += (arr[(y - 1) * width + x - 1] & 255) * sobelMatrix[0][0];
	res += (arr[(y - 1) * width + x    ] & 255) * sobelMatrix[0][1];
	res += (arr[(y - 1) * width + x + 1] & 255) * sobelMatrix[0][2];
	res += (arr[(y + 1) * width + x - 1] & 255) * sobelMatrix[2][0];
	res += (arr[(y + 1) * width + x    ] & 255) * sobelMatrix[2][1];
	res += (arr[(y + 1) * width + x + 1] & 255) * sobelMatrix[2][2];
	return res;
}

//gradient for X
int sumX(int* arr, int x, int y) {
	int res = 0;
	res += (arr[(y - 1) * width + x - 1] & 255) * sobelMatrix[2][0];
	res += (arr[(y    ) * width + x - 1] & 255) * sobelMatrix[2][1];
	res += (arr[(y + 1) * width + x - 1] & 255) * sobelMatrix[2][2];
	res += (arr[(y - 1) * width + x + 1] & 255) * sobelMatrix[0][0];
	res += (arr[(y    ) * width + x + 1] & 255) * sobelMatrix[0][1];
	res += (arr[(y + 1) * width + x + 1] & 255) * sobelMatrix[0][2];
	return res;
}

//work for threads
void *work(void *input) {
	//reading variables from struct
	Args data = *(Args*)input;
	int *sobelArr = data.sobelArr;
	int *pixArr = data.pixArr;
	int i = data.i;
	int max = (i == nThreads - 1) ? height - 1 : part * (i+1) + 1;

	for(int y = part * i + 1; y < max; y++) {
		for(int x = 1; x < width - 1; x++) {
			int tmp = 0;
			int tmpX = sumX(pixArr, x, y);
			int tmpY = sumY(pixArr, x, y);
			tmp = (int)sqrt((tmpX * tmpX + tmpY * tmpY));
			tmp = (tmp > 0) ? (tmp > 255) ? 255 : tmp : 0;
			sobelArr[y * width + x] = (tmp << 16) | (tmp << 8) | tmp;
		}
	}
	return NULL;
}

int main() {
	int rfd, wfd;		//file descriptord for read and write respectievly
	char fileHeader[FILEHEADER];	//14 bytes for header
	char infoHeader[INFOHEADER];	//40 bytes for info
	int fileSize = 0;	//image file size
	int padding = 0;	//padding at the end of each row
	int bufLen = 0;		//length of buffer
	int *pixArr;		//array of pixels of original image
	int *sobelArr;		//array of processed original pixels
	char *buf;			//buffer for reading whole row of image
	struct timespec start, end;	//time variables
	double elapsed;		//elapsed time after threads start
	pthread_t *th;		//threads
	Args *data;			//arguments for threads work

//	printf("Enter filename: ");
//	scanf("%s", fName);
	printf("Enter number of threads: ");
	scanf("%d", &nThreads);

	//open image
	if((rfd = open(fName, O_RDONLY)) < 0) return 1;

	//reading header and info [14 + 40] bytes
	if(read(rfd, fileHeader, FILEHEADER) <= 0) return 2;
	if(read(rfd, infoHeader, INFOHEADER) <= 0) return 3;

	//getting nrcessary data from metadata
	for(int i = 0; i < 4; i++) {
		fileSize |= ((fileHeader[2 + i] << 8 * i) & (0xff << 8 * i));
		width |= ((infoHeader[4 + i] << 8 * i) & (0xff << 8 * i));
		height |= ((infoHeader[8 + i] << 8 * i) & (0xff << 8 * i));
	}
	printf("File size: %d\nWidth: %d\nHeight: %d\n", fileSize, width, height);

	if(nThreads < 1 || nThreads > height - 2) {
		perror("Invalid number. It must be between 1 and image HEIGHT - 2\n");
		return 1;
	}

	//allocating memory for pixels
	pixArr = (int*)malloc(width * height * sizeof(int));
	sobelArr = (int*)malloc(width * height * sizeof(int));
	th = (pthread_t*)malloc((nThreads) * sizeof(pthread_t));
	data = (Args*)malloc((nThreads) * sizeof(Args));
	padding = ((4 - (width * 3) % 4) % 4);
	bufLen = width * height * 3 + padding * height;
	buf = (char*)malloc(bufLen);
	
	//reading image
	read(rfd, buf, bufLen);
	close(rfd);

	//transforming chars into integers
	for(int y = 0; y < height; y++) {
		for(int x = 0; x < width; x++) {
			int i = (	//save row
				((buf[y*width*3+3*x] << 16) & 0xff0000) | 
				((buf[y*width*3+3*x+1] << 8) & 0xff00) | 
				((buf[y*width*3+3*x+2] ) & 0xff)
				);
			pixArr[y * width + x] = (((i >> 16) & 255)+((i >> 8) & 255)+(i & 255)) / 3;
		}
	}
	
	printf("Start timer\n");
	clock_gettime(CLOCK_MONOTONIC, &start);

	//calculating count of rows for one thread
	part = (int)((float)(height - 2) / nThreads);

	//distribute work for "nThreads" threads
	for(int i = 0; i < nThreads; i++) {
		data[i].sobelArr = sobelArr;
		data[i].pixArr = pixArr;
		data[i].i = i;
		if(pthread_create(th + i, NULL, &work, (void*)(data + i)) != 0)
			return 5;
	}

	//wait while threads complete work
	for(int i = 0; i < nThreads; i++)
		if(pthread_join(th[i], NULL) != 0)
			return 6;

	clock_gettime(CLOCK_MONOTONIC, &end);
	//calculating and printing work time
	printf("End timer\n");
	elapsed = end.tv_sec - start.tv_sec;
	elapsed += (end.tv_nsec - start.tv_nsec) / 1000000000.0;
	printf("Time elapsed: %.5lfs\n", elapsed);

	//translating bytes from integers to chars
	for(int y = 1; y < height - 1; y++) {
		for(int x = 1; x < width - 1; x++) {
			buf[y*width*3+x*3] = (sobelArr[y * width + x] >> 16) & 255;
			buf[y*width*3+x*3+1] = (sobelArr[y * width + x] >> 8) & 255;
			buf[y*width*3+x*3+2] = (sobelArr[y * width + x] ) & 255;
		}
	}

	//opening file to write processed image
	if((wfd = open(outName, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) return 4;
	//write metadata
	write(wfd, fileHeader, FILEHEADER);
	write(wfd, infoHeader, INFOHEADER);
	//writing processed pixels
	write(wfd, buf, bufLen);
	close(wfd);

	//free reserved memory
	free(th);
	free(buf);
	free(data);
	free(pixArr);
	free(sobelArr);

	exit(0);
}
