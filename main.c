#include <stddef.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <raylib.h>
#include <pthread.h>
#include <math.h>

#define NOB_IMPLEMENTATION
#include "nob.h"

#define PIPE_OUT 0
#define PIPE_IN 1

// based on https://github.com/tsoding/musializer/blob/master/src/ffmpeg_linux.c
typedef struct{
  int out_fd;
  pid_t pid;
} FFMPEG;

bool ffmpeg_start_screen_capture(
    FFMPEG* self, 
    unsigned int pos_x, 
    unsigned int pos_y,
    unsigned int size_x,
    unsigned int size_y,
    unsigned int res_x, 
    unsigned int res_y
){
  int pipefd[2];
  if(pipe(pipefd) < 0){
    TraceLog(LOG_ERROR, "ffmpeg failed to init pipe");
    return false;
  }

  pid_t child = fork();
  if(child < 0){
    TraceLog(LOG_ERROR, "ffmpeg failed to fork()");
    return false;
  }

  if(child == 0){
    if(dup2(pipefd[PIPE_IN], STDOUT_FILENO) < 0){
      TraceLog(LOG_ERROR, "ffmpeg failed to bind parent pipe to child stdout");
      return false;
    }
    close(pipefd[PIPE_OUT]);

    // redirects ffmpeg logging
#if 0
    int null_fd = open("/dev/null", O_WRONLY);
    if(null_fd < 0){
      TraceLog(LOG_ERROR, "ffmpeg failed to open null pipe");
      return false;
    }
    if(dup2(null_fd, STDERR_FILENO) < 0){
      TraceLog(LOG_ERROR, "ffmpeg failed to bind null pipe to child stderr");
      return false;
    }
    if(dup2(null_fd, STDIN_FILENO) < 0){
      TraceLog(LOG_ERROR, "ffmpeg failed to bind null pipe to child stderr");
      return false;
    }
#endif

    char res_arg[128];
    snprintf(res_arg, sizeof(res_arg), "%ux%u", size_x, size_y); 
    char scale_arg[256];
    int res = snprintf(scale_arg, sizeof(res_arg), "scale=%u:%u", res_x, res_y); 
    assert(res > 10);
    char pos_arg[128];
    snprintf(pos_arg, sizeof(res_arg), ":0.0+%u,%u", pos_x, pos_y); 
    
    fprintf(stderr, "scale_arg res: %d\n", res);
    fprintf(stderr, "scale_arg: %s\n", res_arg);
    int ret = execlp(
      "ffmpeg",
      "ffmpeg",
      "-video_size", res_arg,
      "-framerate", "30",
      "-f", "x11grab",
      "-i", pos_arg,
      "-f", "rawvideo",
      "-vf", scale_arg,
      "-pix_fmt","rgba",
      "-an",
      "-",
      NULL
    );
    if (ret < 0) {
      TraceLog(LOG_ERROR, "FFMPEG CHILD: could not run ffmpeg as a child process: %s", strerror(errno));
      exit(1);
    }
    assert(0 && "unreachable");
    exit(1);
  }

  self->pid = child;
  self->out_fd = pipefd[PIPE_OUT];
  return true;
}

void ffmpeg_read(FFMPEG* self, unsigned char* buffer, unsigned int res_x, unsigned int res_y){
  int frame_size = res_x * res_y * sizeof(Color);
  while(frame_size > 0){
    int res = read(self->out_fd, buffer, frame_size);
    if(res < 0){
      TraceLog(LOG_ERROR, "ffmpeg read error: %s", strerror(errno));
      exit(1);
    }
    frame_size-=res;
    buffer+=res;
  }
}

void ffmpeg_stop(FFMPEG* self){
  close(self->out_fd);
  kill(self->pid, SIGTERM);
  waitpid(self->pid, NULL, 0);
}

typedef enum{
  CaptureDeviceKind_FFMPEG = 0,
  CaptureDeviceKind_END
} CaptureDeviceKind;

typedef struct{
  CaptureDeviceKind device_kind;
  union{
    FFMPEG ffmpeg;
  } source;
  Rectangle capture_area;
  Vector2 resolution;
  Image image;
  bool active;
} CaptureDevice;

void CaptureDevice_init(CaptureDevice* self, Rectangle capture_area, Vector2 output_resolution){
  // TODO: currently only works for linux x11
  // add capture method for:
  //  - wayland
  //  - windows
  //  - macos
  self->device_kind = CaptureDeviceKind_FFMPEG;
  self->capture_area = capture_area;
  self->resolution = output_resolution;
  self->image = GenImageColor(self->resolution.x, self->resolution.y, BLACK);
}

void CaptureDevice_start(CaptureDevice* self){
  self->active = true;
  switch (self->device_kind) {
    case CaptureDeviceKind_FFMPEG:{
      ffmpeg_start_screen_capture(
          &self->source.ffmpeg, 
          self->capture_area.x, 
          self->capture_area.y,
          self->capture_area.width, 
          self->capture_area.height,
          self->resolution.x,
          self->resolution.y);
    } return;
    case CaptureDeviceKind_END: break;
  }
  TraceLog(LOG_ERROR, "CaptureDevice: invalid device kind: %d", self->device_kind);
  exit(1);
}

void CaptureDevice_stop(CaptureDevice* self){
  self->active = false;
  switch (self->device_kind) {
    case CaptureDeviceKind_FFMPEG:{
      ffmpeg_stop(&self->source.ffmpeg);
    } return;
    case CaptureDeviceKind_END: break;
  }
  TraceLog(LOG_ERROR, "CaptureDevice: invalid device kind: %d", self->device_kind);
  exit(1);
}

void CaptureDevice_update_frame(CaptureDevice* self){
  if(!self->active) return;
  switch (self->device_kind) {
    case CaptureDeviceKind_FFMPEG:{
      ffmpeg_read(&self->source.ffmpeg, self->image.data, self->resolution.x, self->resolution.y);
    } return;
    case CaptureDeviceKind_END: break;
  }
  TraceLog(LOG_ERROR, "CaptureDevice: invalid device kind: %d", self->device_kind);
  exit(1);
}

void CaptureDevice_deinit(CaptureDevice* self){
  if(self->active) CaptureDevice_stop(self);
  UnloadImage(self->image);
}

typedef struct SamplerNode SamplerNode;

typedef enum{
  SampleFmt_RGBA = 0,
  SampleFmt_GRAY,
  SampleFmt_END
} SampleFmt;

int SampleFmt_pixel_size(SampleFmt fmt){
  switch (fmt) {
    case SampleFmt_RGBA: return 4; 
    case SampleFmt_GRAY: return 1; 
    case SampleFmt_END: break;
  }
  TraceLog(LOG_ERROR, "SampleFmt_pixel_size: invalid fmt: %d", fmt);
  exit(1);
}

typedef struct{
  float data[4];
  SampleFmt fmt;
} Sample;

typedef struct{
  Image buffer;
} SamplerImportData;

typedef struct{
  SamplerNode* source;
  Image buffer;
} SamplerExportData;

typedef void(*SamplerUnaryOpCallback)(Sample* result, Sample* input, void* user_data);

typedef struct{
  SamplerNode* input;
  SamplerUnaryOpCallback callback;
  void* user_data;
} SamplerUnaryOp;

typedef void(*SamplerBinaryOpCallback)(Sample* result, Sample* a, Sample* b, void* user_data);

typedef struct{
  SamplerNode* a;
  SamplerNode* b;
  SamplerBinaryOpCallback callback;
  void* user_data;
} SamplerBinaryOp;

typedef struct{
  SamplerNode* source;
  float* buffer;
  int delay_frame_count;
  int current_frame;
  int width;
  int height;
  int pixel_size;
} SamplerDelayBuffer;

typedef enum{
  SamplerNodeKind_IMPORT = 0,
  SamplerNodeKind_EXPORT,
  SamplerNodeKind_UNARY_OP,
  SamplerNodeKind_BINARY_OP,
  SamplerNodeKind_DELAY_BUFFER,
  SamplerNodeKind_END
} SamplerNodeKind;

struct SamplerNode{
  SamplerNodeKind kind;
  union {
    SamplerImportData import;
    SamplerExportData export;
    SamplerUnaryOp unary_op;
    SamplerBinaryOp binary_op;
    SamplerDelayBuffer delay_buffer;
  } as;
};

void Sampler_finish_frame(SamplerNode* node){
  switch (node->kind) {
    case SamplerNodeKind_IMPORT:{
      /* no action */
    } return;
    case SamplerNodeKind_EXPORT:{
      Sampler_finish_frame(node->as.export.source);
    } return;
    case SamplerNodeKind_UNARY_OP:{
      Sampler_finish_frame(node->as.unary_op.input);
    } return;
    case SamplerNodeKind_BINARY_OP:{
      Sampler_finish_frame(node->as.binary_op.a);
      Sampler_finish_frame(node->as.binary_op.b);
    } return;
    case SamplerNodeKind_DELAY_BUFFER:{
      Sampler_finish_frame(node->as.delay_buffer.source);
      node->as.delay_buffer.current_frame++;
      node->as.delay_buffer.current_frame%=node->as.delay_buffer.delay_frame_count;
    } return;
    case SamplerNodeKind_END: break;
  }
  TraceLog(LOG_ERROR, "Sampler invalid node kind: %d", node->kind);
  exit(1);
}

void Sampler_resolve_size(SamplerNode* node, int* width, int* height){
  switch (node->kind) {
    case SamplerNodeKind_IMPORT:{
      *width = node->as.import.buffer.width;
      *height = node->as.import.buffer.height;
    } return;
    case SamplerNodeKind_EXPORT:{
      Sampler_resolve_size(node->as.export.source, width, height);
    } return;
    case SamplerNodeKind_UNARY_OP:{
      Sampler_resolve_size(node->as.unary_op.input, width, height);
    } return;
    case SamplerNodeKind_BINARY_OP:{
      int second_width = 0;
      int second_height = 0;
      Sampler_resolve_size(node->as.binary_op.a, width, height);
      Sampler_resolve_size(node->as.binary_op.b, &second_width, &second_height);
      if(second_width != *width || second_height != *height){
        TraceLog(LOG_ERROR, "Sampler on binary op node: input sizes mismatch: %d, %d vs %d, %d", *width, *height, second_width, second_height);
        exit(1);
      }
    } return;
    case SamplerNodeKind_DELAY_BUFFER:{
      Sampler_resolve_size(node->as.delay_buffer.source, width, height);
    } return;
    case SamplerNodeKind_END: break;
  }
  TraceLog(LOG_ERROR, "Sampler invalid node kind: %d", node->kind);
  exit(1);
}

void Sampler_sample(SamplerNode* node, Sample* result, int x, int y){
  switch (node->kind) {
    case SamplerNodeKind_IMPORT:{
      if(node->as.import.buffer.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8){
        TraceLog(LOG_ERROR, "Sampler on import node: buffer has unsupported pixel format: %d", node->as.import.buffer.format);
        exit(1);
      }
      result->fmt = SampleFmt_RGBA;
      Image* buffer = &node->as.import.buffer;
      unsigned char* pixel = ((unsigned char*)buffer->data)+(x+y*buffer->width)*sizeof(unsigned char)*4;
      result->data[0] = ((float)pixel[0]);
      result->data[1] = ((float)pixel[1]);
      result->data[2] = ((float)pixel[2]);
      result->data[3] = ((float)pixel[3]);
    } return;
    case SamplerNodeKind_EXPORT:{
      Sampler_sample(node->as.export.source, result, x, y);
      if(result->fmt != SampleFmt_RGBA){
        TraceLog(LOG_ERROR, "Sampler on export node: expected RGBA result, got %d", node->as.import.buffer.format);
        exit(1);
      }
      if(node->as.export.buffer.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8){
        TraceLog(LOG_ERROR, "Sampler on export node: buffer has unsupported pixel format: %d", node->as.import.buffer.format);
        exit(1);
      }
      Image* buffer = &node->as.export.buffer;
      unsigned char* pixel = ((unsigned char*)buffer->data)+(x+y*buffer->width)*sizeof(unsigned char)*4;
      pixel[0] = (unsigned char)result->data[0];
      pixel[1] = (unsigned char)result->data[1];
      pixel[2] = (unsigned char)result->data[2];
      pixel[3] = (unsigned char)result->data[3];
    } return;
    case SamplerNodeKind_UNARY_OP:{
      Sample input = {0};
      Sampler_sample(node->as.unary_op.input, &input, x, y);
      node->as.unary_op.callback(result, &input, node->as.unary_op.user_data);
    } return;
    case SamplerNodeKind_BINARY_OP:{
      Sample a;
      Sample b;
      Sampler_sample(node->as.binary_op.a, &a, x, y);
      Sampler_sample(node->as.binary_op.b, &b, x, y);
      node->as.binary_op.callback(result, &a, &b, node->as.binary_op.user_data);
    } return;
    case SamplerNodeKind_DELAY_BUFFER:{
      SamplerDelayBuffer* dbuf = &node->as.delay_buffer;
      Sampler_sample(dbuf->source, result, x, y);
      
      // new sample to current frame
      float* current_pixel = dbuf->buffer + 
        (x+y*dbuf->width+dbuf->current_frame*dbuf->width*dbuf->height)*dbuf->pixel_size;
      
      // oldest frame as result
      int oldest_frame = (dbuf->current_frame+1)%dbuf->delay_frame_count;
      float* delayed_pixel = dbuf->buffer + 
        (x+y*dbuf->width+oldest_frame*dbuf->width*dbuf->height)*dbuf->pixel_size;

      for(int i = 0; i < dbuf->pixel_size; ++i){
        current_pixel[i] = result->data[i];
        result->data[i] = delayed_pixel[i];
      }
    } return;
    case SamplerNodeKind_END: break;
  }
  TraceLog(LOG_ERROR, "Sampler invalid node kind: %d", node->kind);
  exit(1);
}

typedef struct{
  SamplerNode* root;
  int base_x;
  int base_y;
  int width;
  int height;
} SamplerTaskInfo;
void* Sampler_run_task(void* args){
  SamplerTaskInfo* info = args;
  Sample result = {0};
  for(int y = 0; y < info->height; ++y){
    for(int x = 0; x < info->width; ++x){
      Sampler_sample(info->root, &result, x+info->base_x, y+info->base_y);
      Image* buffer = &info->root->as.export.buffer;
    }
  }
  return NULL;
}

void Sampler_thread_run(SamplerNode* root){
  int thread_count = 16;
  pthread_t threads[thread_count];
  SamplerTaskInfo task_infos[thread_count];

  int width = 0;
  int height = 0;
  Sampler_resolve_size(root, &width, &height);
  for(int thread = 0; thread < thread_count; ++thread){
    task_infos[thread] = (SamplerTaskInfo){
      .root = root,
      .base_x = (width/thread_count)*thread,
      .base_y = 0,
      .width = width/thread_count,
      .height = height,
    };
    pthread_create(&threads[thread], NULL, Sampler_run_task, &task_infos[thread]);
  }
  for(int thread = 0; thread < thread_count; ++thread){
    pthread_join(threads[thread], NULL);
  }
  Sampler_finish_frame(root);
}

void Sampler_run(SamplerNode* root){
  Sample result = {0};
  int width = 0;
  int height = 0;
  Sampler_resolve_size(root, &width, &height);
  for(int y = 0; y < height; ++y){
    for(int x = 0; x < width; ++x){
      Sampler_sample(root, &result, x, y);
      Image* buffer = &root->as.export.buffer;
    }
  }
  Sampler_finish_frame(root);
}

SamplerNode* SamplerNode_new(SamplerNodeKind kind){
  SamplerNode* self = calloc(1, sizeof(SamplerNode));
  assert(self);
  self->kind = kind;
  return self;
}

SamplerNode* Sampler_import(Image image){
  SamplerNode* self = SamplerNode_new(SamplerNodeKind_IMPORT);
  assert(image.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
  self->as.import.buffer = image;
  return self;
}

SamplerNode* Sampler_export(Image image, SamplerNode* source){
  SamplerNode* self = SamplerNode_new(SamplerNodeKind_EXPORT);
  assert(image.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
  self->as.export.buffer = image;
  self->as.export.source = source;
  return self;
}

SamplerNode* SamplerUnaryOp_new(SamplerNode* input, SamplerUnaryOpCallback callback, void* user_data){
  SamplerNode* self = SamplerNode_new(SamplerNodeKind_UNARY_OP);
  self->as.unary_op.input = input;
  self->as.unary_op.callback = callback;
  self->as.unary_op.user_data = user_data;
  return self;
}

SamplerNode* SamplerBinaryOp_new(SamplerNode* a, SamplerNode* b, SamplerBinaryOpCallback callback, void* user_data){
  SamplerNode* self = SamplerNode_new(SamplerNodeKind_BINARY_OP);
  self->as.binary_op.a = a;
  self->as.binary_op.b = b;
  self->as.binary_op.callback = callback;
  self->as.binary_op.user_data = user_data;
  return self;
}

SamplerNode* SamplerDelayBuffer_new(SamplerNode* source, int frame_delay){
  SamplerNode* self = SamplerNode_new(SamplerNodeKind_DELAY_BUFFER);
  self->as.delay_buffer.delay_frame_count = frame_delay+1;
  Sampler_resolve_size(source, &self->as.delay_buffer.width, &self->as.delay_buffer.height);
  self->as.delay_buffer.source = source;
  Sample test_sample = {0};
  Sampler_sample(source, &test_sample, 0, 0);
  self->as.delay_buffer.pixel_size = SampleFmt_pixel_size(test_sample.fmt);
  self->as.delay_buffer.buffer = calloc(
      self->as.delay_buffer.width*
      self->as.delay_buffer.height*
      self->as.delay_buffer.delay_frame_count, 
    sizeof(float)*self->as.delay_buffer.pixel_size);
  assert(self->as.delay_buffer.buffer);
  return self;
}

void UnOp_to_gray(Sample* result, Sample* input, void* user_data){
  (void)user_data;
  assert(input->fmt == SampleFmt_RGBA);
  result->fmt = SampleFmt_GRAY;
  result->data[0] = (
      input->data[0] +
      input->data[1] +
      input->data[2]
    )/3.0f;
}

void UnOp_to_rgba(Sample* result, Sample* input, void* user_data){
  (void)user_data;
  assert(input->fmt == SampleFmt_GRAY);
  result->fmt = SampleFmt_RGBA;
  result->data[0] = input->data[0];
  result->data[1] = input->data[0];
  result->data[2] = input->data[0];
  result->data[3] = 255.0f;
}

void BinOp_diff(Sample* result, Sample* a, Sample* b, void* user_data){
  (void)user_data;
  int pixel_size = SampleFmt_pixel_size(a->fmt);
  for(int i = 0; i < pixel_size; ++i){
    result->data[i] = fabsf(a->data[i] - b->data[i]);
    result->data[i] = 255.0f-(b->data[i]-a->data[i]);
  }
}

int main(void){
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(800, 600, "screen capture");

  CaptureDevice capture_device = {0};
  Rectangle capture_area = {0,0, GetMonitorWidth(0), GetMonitorHeight(0)};
  CaptureDevice_init(&capture_device, capture_area, (Vector2){capture_area.width/2.0f, capture_area.height/2.0f});

  Image displays[4] = {0};
  for(int i = 0; i < 4; ++i){
    displays[i] = ImageCopy(capture_device.image);
  }
  SamplerNode* import = Sampler_import(capture_device.image);
  SamplerNode* base = Sampler_export(displays[0], import);
  SamplerNode* delayed = Sampler_export(displays[1], 
    SamplerDelayBuffer_new(import, 30)
  );
  SamplerNode* diff = Sampler_export(displays[2],
    SamplerBinaryOp_new(base, delayed, BinOp_diff, NULL)
  );

  Texture textures[4];
  for(int i = 0; i < 4; ++i){
    textures[i] = LoadTextureFromImage(capture_device.image);
  }
  
  CaptureDevice_start(&capture_device);

  while(!WindowShouldClose()){
    CaptureDevice_update_frame(&capture_device);
    Sampler_thread_run(base);
    Sampler_thread_run(delayed);
    Sampler_thread_run(diff);
    for(int i = 0; i < 4; ++i){
      UpdateTexture(textures[i], displays[i].data);
    }

    BeginDrawing();
      ClearBackground(BLACK);
      DrawTexture(textures[0], 0, 0, WHITE);
      DrawTexture(textures[1], 0, 1080/2, WHITE);
      DrawTexture(textures[2], 1920/2, 0, WHITE);
    EndDrawing();
  }

  CaptureDevice_stop(&capture_device);
  CloseWindow();
  return 0;
}
