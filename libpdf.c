
#include "mupdf/fitz.h"
#include "mupdf/pdf.h"

#define EXPORT_DLL  __declspec(dllexport)
static pdf_document   *doc = NULL;
static fz_document    *fzdoc = NULL;
static fz_context     *ctx = NULL;
static int            images = 0;
static int			  image_obj_num[100];
static fz_text_sheet  *sheet = NULL;
FILE *ofp;

void writepixmap(fz_context *ctx, fz_pixmap *pix, char *file, int rgb)
{
  char buf[1024];
  fz_pixmap *converted = NULL;
  if (!pix)
    {
      return;
    }
  if (rgb && pix->colorspace && pix->colorspace != fz_device_rgb(ctx))
    {
      fz_irect bbox;
      converted = fz_new_pixmap_with_bbox(ctx,fz_device_rgb(ctx),fz_pixmap_bbox(ctx,pix,&bbox));
      fz_convert_pixmap(ctx,converted,pix);
      pix=converted;
    }
  if(pix->n<=4)
    {
      snprintf(buf,sizeof(buf),"%s.png",file);
      fz_write_png(ctx,pix,buf,0);
    }
  else
    {
      snprintf(buf,sizeof(buf),"%s.pam",file);
      fz_write_pam(ctx,pix,buf,0);
    }
  fz_drop_pixmap(ctx,converted);
}

void saveimage(char *dest_path,int num)
{
  fz_image   *image;
  fz_pixmap  *pix;
  pdf_obj    *ref;
  char       buf[200];
  
  ref = pdf_new_indirect(doc,num,0);
  
  //save image into file
  image = pdf_load_image(doc,ref);
  pix = fz_new_pixmap_from_image(ctx,image,0,0);
  fz_drop_image(ctx,image);

  snprintf(buf,sizeof(buf),"%s/img-%04d",dest_path,num);
  //rgb mode
  writepixmap(ctx,pix,buf,1);

  fz_drop_pixmap(ctx,pix);
  pdf_drop_obj(ref);
}

EXPORT_DLL int init_image(char *filename,char *password)
{
  int result = 0;
  FILE * f;
  f=fopen("testfile.txt","a");
  fprintf(f,"%s",filename);
  fclose(f);
  ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
  if (!ctx)
    {
      //can not initialise context
      result = 1;
    }
  doc = pdf_open_document_no_run(ctx,filename);      

  if (pdf_needs_password(doc))
    {
      if (!pdf_authenticate_password(doc,password))
        {
          // cannot authenticate password
          result = 2;
        }
      result = 0;
    }
  // pdf sucessfully loaded
  return result;
}

EXPORT_DLL int page_counts()
{
  int pages = 0;
  pages = pdf_count_pages(doc);
  return pages;
}

EXPORT_DLL void extractImage(char *dest_path,int objnum)
{
  pdf_obj  *obj;
  obj = pdf_load_object(doc,objnum,0);
 if (isimage(obj))
  saveimage(dest_path,objnum);
  pdf_drop_obj(obj);
}

EXPORT_DLL void destroy_image()
{
  if (doc)
    {
      pdf_close_document(doc);
      doc = NULL;
      fz_free_context(ctx);
    }  
}
static void analysis_image_info(int page,pdf_obj *pageref,pdf_obj *pageobj,pdf_obj *dict)
{
  int i,n;

  n = pdf_dict_len(dict);

  for (i = 0; i < n; ++i)
    {
      pdf_obj	*imagedict;
      pdf_obj	*type;
      
      imagedict = pdf_dict_get_val(dict,i);
      if (!pdf_is_dict(imagedict)) continue;

      type = pdf_dict_gets(imagedict,"Subtype");
      if (strcmp(pdf_to_name(type),"Image")) continue;
      images++;
      image_obj_num[images-1]=pdf_to_num(imagedict);
    }
}

static void analysis_resource_info(int page,pdf_obj *rsrc)
{
  pdf_obj	*pageobj;
  pdf_obj	*pageref;
  pdf_obj	*xobj;
  pdf_obj	*subrsrc;

  pageref = pdf_lookup_page_obj(doc,page-1);
  pageobj = pdf_resolve_indirect(pageref);

  xobj = pdf_dict_gets(rsrc,"XObject");
  if (xobj)
    {
      analysis_image_info(page,pageref,pageobj,xobj);
      int n = pdf_dict_len(xobj);
      int i = 0;
      for (i = 0; i < n; i++)
        {
          pdf_obj *obj = pdf_dict_get_val(xobj,i);
          subrsrc = pdf_dict_gets(obj,"Resources");
          if (subrsrc && pdf_objcmp(rsrc,subrsrc))
            {
              analysis_resource_info(page,subrsrc);
            }
        }
    }
}

EXPORT_DLL int analysis_page_info(int page)
{
  pdf_obj *pageobj;
  pdf_obj *pageref;
  pdf_obj *rsrc;

  int result=0;
  pageref = pdf_lookup_page_obj(doc,page-1);
  pageobj = pdf_resolve_indirect(pageref);
  
  if (!pageobj)
    {
      // cannot retrieve info from page
      result = 1;
    }

  rsrc = pdf_dict_gets(pageobj,"Resources");
  analysis_resource_info(page,rsrc);
  return result;
}

EXPORT_DLL void get_image_obj_num(char imagenum[])
{
  char *pos= imagenum;

  int i = 0;

  for (i = 0; i < images; ++i)
    {
      if (i)
        {
          pos += sprintf(pos,",");
        }
	  pos += sprintf(pos,"%d",image_obj_num[i]);
    }
}
int isimage(pdf_obj *obj)
{
	pdf_obj *type = pdf_dict_gets(obj, "Subtype");
	return pdf_is_name(type) && !strcmp(pdf_to_name(type), "Image");
}

EXPORT_DLL void export_all_image(char *dest_path)
{
  int len=pdf_count_objects(doc);
  int n;
  for(n=0;n<len;n++)
    {
       extractImage(dest_path,n);
    }
}
EXPORT_DLL int init_txt(char* filename,char* password)
{
  int result=0;
  fz_var(fzdoc);
  ctx = fz_new_context(NULL,NULL,FZ_STORE_DEFAULT);
  if (!ctx)
    {
      //initialise error
      result = 1;
    }
  fzdoc = fz_open_document(ctx,filename);
  if (fz_needs_password(fzdoc))
     {
       if (!fz_authenticate_password(fzdoc,password))
         {
           //authenticate error
           result = 2;
         }
     }
 }
EXPORT_DLL int extractText(int pagenum,char* textfile)
{
  fz_page      *page;
  fz_device    *dev = NULL;
  fz_text_page *text = NULL;
  fz_cookie    cookie = {0};
  fz_output		*out = NULL;

  int 	result = 0;

  ofp = fopen(textfile,"w");
  fz_var(dev);
  fz_var(text);
  fz_var(out);

  fz_try(ctx)
  {

    out = fz_new_output_with_file(ctx,ofp);
    sheet = fz_new_text_sheet(ctx);
 
    page = fz_load_page(fzdoc,pagenum-1);
    text = fz_new_text_page(ctx);
    dev = fz_new_text_device(ctx,sheet,text);
    fz_run_page(fzdoc,page,dev,&fz_identity,&cookie);
    
    fz_print_text_page(ctx,out,text);
  }
  fz_always(ctx)
  {
    fz_free_device(dev);
    dev = NULL;
    fz_free_text_page(ctx,text);
  }
  fz_catch(ctx)
  {
    fz_free_page(fzdoc,page);
    //exception
    result = 3;
  }
  fz_close_output(out);
  fclose(ofp);
  return result;
}

EXPORT_DLL void destroy_txt()
{
  fz_close_document(fzdoc);
  fzdoc = NULL;
  fz_free_context(ctx);

}

int main(int argc, char *argv[])
{
  char *filename= argv[1];
  init_image(filename,"");
  int pages=page_counts();
  printf("pages:%d",pages);
  export_all_image(".");
  destroy_image();
  init_txt(filename,"");
  int n=0;
  for( n=1; n<pages+1;n++){
    char txt[5];
    sprintf(txt,"%d.txt",n);
    extractText(n,txt);
  }
  destroy_txt();
  return 0;
}
