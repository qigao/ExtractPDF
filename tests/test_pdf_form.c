#include <extractpdf/extractpdf.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_impl(int ok, const char *expr, int line)
{
    if (!ok) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expr); exit(EXIT_FAILURE); }
}
#define CHECK(x) check_impl((x), #x, __LINE__)
static int close_float(float a,float b){float d=a-b;if(d<0)d=-d;return d<0.01f;}

static void compile_surface(void)
{
    extractpdf_form *form=NULL;extractpdf_form_field_type type=EXTRACTPDF_FORM_FIELD_UNKNOWN;
    extractpdf_form_value_presence presence=EXTRACTPDF_FORM_VALUE_NOT_APPLICABLE;extractpdf_form_value_kind vk=EXTRACTPDF_FORM_VALUE_UTF8;
    extractpdf_form_option_kind ok=EXTRACTPDF_FORM_OPTION_BUTTON_STATE;extractpdf_form_field_info fi={0};extractpdf_form_value_info vi={0};
    extractpdf_form_option_info oi={0};extractpdf_form_widget_info wi={0};extractpdf_document *d=NULL;const char *s=NULL;size_t n=0,c=0;
    fi.struct_size=sizeof(fi);vi.struct_size=sizeof(vi);oi.struct_size=sizeof(oi);wi.struct_size=sizeof(wi);(void)type;(void)presence;(void)vk;(void)ok;
    if(0){(void)extractpdf_document_form(d,&form);(void)extractpdf_form_field_count(form,&c);(void)extractpdf_form_field_get_info(form,0,&fi);
    (void)extractpdf_form_field_name(form,0,&s,&n);(void)extractpdf_form_field_label(form,0,&s,&n);(void)extractpdf_form_field_value_get_info(form,0,0,&vi);
    (void)extractpdf_form_field_value_utf8(form,0,0,&s,&n);(void)extractpdf_form_field_option_get_info(form,0,0,&oi);(void)extractpdf_form_field_option_export(form,0,0,&s,&n);
    (void)extractpdf_form_field_option_display(form,0,0,&s,&n);(void)extractpdf_form_widget_count(form,&c);(void)extractpdf_form_widget_get_info(form,0,&wi);extractpdf_drop_form(form);}
}

static void expect_empty(const char *path)
{extractpdf_document *d=NULL;extractpdf_form *f=NULL;size_t a=99,b=99;CHECK(extractpdf_open(path,NULL,&d)==EXTRACTPDF_OK);CHECK(extractpdf_document_form(d,&f)==EXTRACTPDF_OK);CHECK(f!=NULL);extractpdf_close(d);CHECK(extractpdf_form_field_count(f,&a)==EXTRACTPDF_OK);CHECK(a==0);CHECK(extractpdf_form_widget_count(f,&b)==EXTRACTPDF_OK);CHECK(b==0);extractpdf_drop_form(f);}
static void expect_string(const extractpdf_form *f,size_t i,int label,const char *e,int present)
{const char *s=(const char *)(uintptr_t)1;size_t n=99;extractpdf_status st=label?extractpdf_form_field_label(f,i,&s,&n):extractpdf_form_field_name(f,i,&s,&n);CHECK(st==EXTRACTPDF_OK);if(!present){CHECK(s==NULL);CHECK(n==0);return;}CHECK(s!=NULL);CHECK(n==strlen(e));CHECK(memcmp(s,e,n)==0);CHECK(s[n]=='\0');}
static void expect_extract_status(const char *path,extractpdf_status expected)
{int sentinel=0;extractpdf_document *d=NULL;extractpdf_form *f=(extractpdf_form *)&sentinel;CHECK(extractpdf_open(path,NULL,&d)==EXTRACTPDF_OK);CHECK(extractpdf_document_form(d,&f)==expected);CHECK(f==NULL);extractpdf_close(d);}

static void test_api_shell(void)
{int sentinel=0;extractpdf_form *f=(extractpdf_form *)&sentinel;size_t c=99;const char *s=(const char *)&sentinel;size_t n=99;
CHECK(EXTRACTPDF_FORM_FIELD_UNKNOWN==0);CHECK(EXTRACTPDF_FORM_FIELD_SIGNATURE==7);CHECK(EXTRACTPDF_FORM_VALUE_NOT_APPLICABLE==0);CHECK(EXTRACTPDF_FORM_VALUE_PRESENT==2);CHECK(EXTRACTPDF_FORM_VALUE_UTF8==1);CHECK(EXTRACTPDF_FORM_VALUE_OPTION==2);CHECK(EXTRACTPDF_FORM_OPTION_BUTTON_STATE==1);CHECK(EXTRACTPDF_FORM_OPTION_CHOICE==2);
CHECK(extractpdf_document_form(NULL,&f)==EXTRACTPDF_ERROR_ARGUMENT);CHECK(f==NULL);CHECK(extractpdf_document_form(NULL,NULL)==EXTRACTPDF_ERROR_ARGUMENT);CHECK(extractpdf_form_field_count(NULL,&c)==EXTRACTPDF_ERROR_ARGUMENT);CHECK(c==0);CHECK(extractpdf_form_widget_count(NULL,&c)==EXTRACTPDF_ERROR_ARGUMENT);CHECK(c==0);CHECK(extractpdf_form_field_name(NULL,0,&s,&n)==EXTRACTPDF_ERROR_ARGUMENT);CHECK(s==NULL&&n==0);extractpdf_drop_form(NULL);}
static void test_empty_and_non_pdf(void)
{int sentinel=0;extractpdf_document *d=NULL;extractpdf_form *f=(extractpdf_form *)&sentinel;expect_empty(NO_ACROFORM_PDF);expect_empty(ACROFORM_NO_FIELDS_PDF);expect_empty(ACROFORM_EMPTY_FIELDS_PDF);CHECK(extractpdf_open(NON_PDF,NULL,&d)==EXTRACTPDF_OK);CHECK(extractpdf_document_form(d,&f)==EXTRACTPDF_ERROR_UNSUPPORTED);CHECK(f==NULL);extractpdf_close(d);}

static void expect_structure_info(const extractpdf_form *f,size_t i)
{extractpdf_form_field_info x={0};x.struct_size=sizeof(x);CHECK(extractpdf_form_field_get_info(f,i,&x)==EXTRACTPDF_OK);CHECK(x.type==EXTRACTPDF_FORM_FIELD_TEXT);CHECK(x.flags==0);CHECK(x.value_presence==EXTRACTPDF_FORM_VALUE_MISSING);CHECK(x.value_count==0);CHECK(x.option_count==0);CHECK(x.widget_count==0);}
static void test_structure(void)
{extractpdf_document *d=NULL;extractpdf_form *f=NULL;size_t c=99;CHECK(extractpdf_open(ACROFORM_STRUCTURE_PDF,NULL,&d)==EXTRACTPDF_OK);CHECK(extractpdf_document_form(d,&f)==EXTRACTPDF_OK);CHECK(extractpdf_form_field_count(f,&c)==EXTRACTPDF_OK);CHECK(c==4);CHECK(extractpdf_form_widget_count(f,&c)==EXTRACTPDF_OK);CHECK(c==0);
expect_structure_info(f,0);expect_string(f,0,0,"profile.nickname",1);expect_string(f,0,1,"Profile label",1);expect_structure_info(f,1);expect_string(f,1,0,"repeat",1);expect_string(f,1,1,"",0);expect_structure_info(f,2);expect_string(f,2,0,"",0);expect_structure_info(f,3);expect_string(f,3,0,"",1);extractpdf_drop_form(f);extractpdf_close(d);
expect_extract_status(ACROFORM_BAD_ROOT_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_BAD_FIELDS_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_BAD_KID_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_CYCLE_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_REPEATED_NODE_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_PARENT_MISMATCH_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_ROOT_PARENT_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_DEPTH_257_PDF,EXTRACTPDF_ERROR_UNSUPPORTED);expect_extract_status(ACROFORM_MIXED_GROUP_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_GROUP_CONFLICT_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_DUPLICATE_NAME_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_PERIOD_NAME_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_BAD_FT_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_BAD_FF_PDF,EXTRACTPDF_ERROR_FORMAT);}

static void expect_widget_field(const extractpdf_form *f,size_t i,extractpdf_form_field_type t,extractpdf_form_value_presence p,size_t o,size_t w)
{extractpdf_form_field_info x={0};x.struct_size=sizeof(x);CHECK(extractpdf_form_field_get_info(f,i,&x)==EXTRACTPDF_OK);CHECK(x.type==t);CHECK(x.value_presence==p);CHECK(x.value_count==0);CHECK(x.option_count==o);CHECK(x.widget_count==w);}
static void expect_button_option(const extractpdf_form *f,size_t i,size_t o)
{extractpdf_form_option_info x={0};x.struct_size=sizeof(x);CHECK(extractpdf_form_field_option_get_info(f,i,o,&x)==EXTRACTPDF_OK);CHECK(x.kind==EXTRACTPDF_FORM_OPTION_BUTTON_STATE);}
static void expect_widget(const extractpdf_form *f,size_t i,size_t field,int page,float x0,float y0,float x1,float y1,uint32_t flags,size_t opt)
{extractpdf_form_widget_info x={0};x.struct_size=sizeof(x);CHECK(extractpdf_form_widget_get_info(f,i,&x)==EXTRACTPDF_OK);CHECK(x.field_index==field);CHECK(x.page_index==page);CHECK(close_float(x.bounds.x0,x0));CHECK(close_float(x.bounds.y0,y0));CHECK(close_float(x.bounds.x1,x1));CHECK(close_float(x.bounds.y1,y1));CHECK(x.flags==flags);CHECK(x.button_option_index==opt);}
static void test_widgets(void)
{extractpdf_document *d=NULL;extractpdf_form *f=NULL;size_t c=99;CHECK(extractpdf_open(ACROFORM_WIDGETS_PDF,NULL,&d)==EXTRACTPDF_OK);CHECK(extractpdf_document_form(d,&f)==EXTRACTPDF_OK);CHECK(extractpdf_form_field_count(f,&c)==EXTRACTPDF_OK);CHECK(c==4);CHECK(extractpdf_form_widget_count(f,&c)==EXTRACTPDF_OK);CHECK(c==7);
expect_widget_field(f,0,EXTRACTPDF_FORM_FIELD_TEXT,EXTRACTPDF_FORM_VALUE_MISSING,0,1);expect_widget_field(f,1,EXTRACTPDF_FORM_FIELD_CHECKBOX,EXTRACTPDF_FORM_VALUE_MISSING,1,2);expect_widget_field(f,2,EXTRACTPDF_FORM_FIELD_RADIO_BUTTON,EXTRACTPDF_FORM_VALUE_MISSING,2,3);expect_widget_field(f,3,EXTRACTPDF_FORM_FIELD_UNKNOWN,EXTRACTPDF_FORM_VALUE_NOT_APPLICABLE,0,1);expect_button_option(f,1,0);expect_button_option(f,2,0);expect_button_option(f,2,1);
expect_widget(f,0,0,0,10,10,40,30,0,SIZE_MAX);expect_widget(f,1,1,0,50,10,70,30,0,0);expect_widget(f,2,2,0,80,10,100,30,0,0);expect_widget(f,3,1,1,10,10,30,30,UINT32_C(2147483649),0);expect_widget(f,4,2,1,40,10,60,30,0,1);expect_widget(f,5,3,1,70,10,100,30,0,SIZE_MAX);expect_widget(f,6,2,2,10,10,30,30,0,0);extractpdf_drop_form(f);extractpdf_close(d);
CHECK(extractpdf_open(ACROFORM_ANNOTS_NONARRAY_PDF,NULL,&d)==EXTRACTPDF_OK);CHECK(extractpdf_document_form(d,&f)==EXTRACTPDF_OK);CHECK(extractpdf_form_widget_count(f,&c)==EXTRACTPDF_OK);CHECK(c==0);extractpdf_drop_form(f);extractpdf_close(d);expect_extract_status(ACROFORM_BAD_BUTTON_AP_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_ORPHAN_WIDGET_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_MISSING_WIDGET_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_DUPLICATE_WIDGET_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_P_MISMATCH_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_DIRECT_WIDGET_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_BAD_WIDGET_RECT_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_BAD_WIDGET_FLAGS_PDF,EXTRACTPDF_ERROR_FORMAT);}

static void expect_scalar_field(const extractpdf_form *f,size_t i,extractpdf_form_field_type t,extractpdf_form_value_presence p,size_t values,int signed_state)
{extractpdf_form_field_info x={0};x.struct_size=sizeof(x);CHECK(extractpdf_form_field_get_info(f,i,&x)==EXTRACTPDF_OK);CHECK(x.type==t);CHECK(x.value_presence==p);CHECK(x.value_count==values);CHECK(x.option_count==0);CHECK(x.is_signed==signed_state);}
static void expect_utf8_value(const extractpdf_form *f,size_t i,size_t vi,const char *e)
{extractpdf_form_value_info x={0};const char *s=(const char *)(uintptr_t)1;size_t n=99;x.struct_size=sizeof(x);CHECK(extractpdf_form_field_value_get_info(f,i,vi,&x)==EXTRACTPDF_OK);CHECK(x.kind==EXTRACTPDF_FORM_VALUE_UTF8);CHECK(x.option_index==SIZE_MAX);CHECK(extractpdf_form_field_value_utf8(f,i,vi,&s,&n)==EXTRACTPDF_OK);CHECK(s!=NULL);CHECK(n==strlen(e));CHECK(memcmp(s,e,n)==0);CHECK(s[n]=='\0');}
static void test_scalar_values_and_full_api(void)
{extractpdf_document *d=NULL;extractpdf_form *f=NULL;size_t c=99;extractpdf_form_field_info small={0},info={0};CHECK(extractpdf_open(ACROFORM_SCALARS_PDF,NULL,&d)==EXTRACTPDF_OK);CHECK(extractpdf_document_form(d,&f)==EXTRACTPDF_OK);CHECK(extractpdf_form_field_count(f,&c)==EXTRACTPDF_OK);CHECK(c==7);expect_scalar_field(f,0,EXTRACTPDF_FORM_FIELD_TEXT,EXTRACTPDF_FORM_VALUE_MISSING,0,0);expect_scalar_field(f,1,EXTRACTPDF_FORM_FIELD_TEXT,EXTRACTPDF_FORM_VALUE_PRESENT,1,0);expect_utf8_value(f,1,0,"");expect_scalar_field(f,2,EXTRACTPDF_FORM_FIELD_TEXT,EXTRACTPDF_FORM_VALUE_PRESENT,1,0);expect_utf8_value(f,2,0,"hello");expect_string(f,2,1,"",1);expect_scalar_field(f,3,EXTRACTPDF_FORM_FIELD_PUSH_BUTTON,EXTRACTPDF_FORM_VALUE_NOT_APPLICABLE,0,0);expect_scalar_field(f,4,EXTRACTPDF_FORM_FIELD_SIGNATURE,EXTRACTPDF_FORM_VALUE_NOT_APPLICABLE,0,0);expect_scalar_field(f,5,EXTRACTPDF_FORM_FIELD_SIGNATURE,EXTRACTPDF_FORM_VALUE_NOT_APPLICABLE,0,1);expect_scalar_field(f,6,EXTRACTPDF_FORM_FIELD_UNKNOWN,EXTRACTPDF_FORM_VALUE_NOT_APPLICABLE,0,0);small.struct_size=offsetof(extractpdf_form_field_info,is_signed);CHECK(extractpdf_form_field_get_info(f,0,&small)==EXTRACTPDF_ERROR_ARGUMENT);info.struct_size=sizeof(info);info.type=EXTRACTPDF_FORM_FIELD_SIGNATURE;info.flags=UINT32_MAX;CHECK(extractpdf_form_field_get_info(f,99,&info)==EXTRACTPDF_ERROR_ARGUMENT);CHECK(info.type==EXTRACTPDF_FORM_FIELD_UNKNOWN);CHECK(info.flags==0);extractpdf_drop_form(f);extractpdf_close(d);expect_extract_status(ACROFORM_BAD_VALUE_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_BAD_SIGNATURE_PDF,EXTRACTPDF_ERROR_FORMAT);}

static void expect_choice_field(const extractpdf_form *f,size_t i,extractpdf_form_field_type t,uint32_t flags,size_t options,size_t values,int multi)
{extractpdf_form_field_info x={0};x.struct_size=sizeof(x);CHECK(extractpdf_form_field_get_info(f,i,&x)==EXTRACTPDF_OK);CHECK(x.type==t);CHECK(x.flags==flags);CHECK(x.value_presence==EXTRACTPDF_FORM_VALUE_PRESENT);CHECK(x.option_count==options);CHECK(x.value_count==values);CHECK(x.is_multiselect==multi);}
static void expect_choice_option(const extractpdf_form *f,size_t field,size_t option,const char *export_text,const char *display_text)
{extractpdf_form_option_info oi={0};const char *s=NULL;size_t n=0;oi.struct_size=sizeof(oi);CHECK(extractpdf_form_field_option_get_info(f,field,option,&oi)==EXTRACTPDF_OK);CHECK(oi.kind==EXTRACTPDF_FORM_OPTION_CHOICE);CHECK(extractpdf_form_field_option_export(f,field,option,&s,&n)==EXTRACTPDF_OK);CHECK(n==strlen(export_text));CHECK(memcmp(s,export_text,n)==0);CHECK(extractpdf_form_field_option_display(f,field,option,&s,&n)==EXTRACTPDF_OK);CHECK(n==strlen(display_text));CHECK(memcmp(s,display_text,n)==0);}
static void expect_option_value(const extractpdf_form *f,size_t field,size_t value_index,size_t option_index)
{extractpdf_form_value_info v={0};const char *s=(const char *)(uintptr_t)1;size_t n=99;v.struct_size=sizeof(v);CHECK(extractpdf_form_field_value_get_info(f,field,value_index,&v)==EXTRACTPDF_OK);CHECK(v.kind==EXTRACTPDF_FORM_VALUE_OPTION);CHECK(v.option_index==option_index);CHECK(extractpdf_form_field_value_utf8(f,field,value_index,&s,&n)==EXTRACTPDF_ERROR_UNSUPPORTED);CHECK(s==NULL);CHECK(n==0);}
static void test_choice_values(void)
{extractpdf_document *d=NULL;extractpdf_form *f=NULL;size_t c=99;CHECK(extractpdf_open(ACROFORM_CHOICE_PDF,NULL,&d)==EXTRACTPDF_OK);CHECK(extractpdf_document_form(d,&f)==EXTRACTPDF_OK);CHECK(extractpdf_form_field_count(f,&c)==EXTRACTPDF_OK);CHECK(c==4);
expect_choice_field(f,0,EXTRACTPDF_FORM_FIELD_COMBO_BOX,UINT32_C(131072),2,1,0);expect_choice_option(f,0,0,"US","United States");expect_choice_option(f,0,1,"JP","Japan");expect_option_value(f,0,0,1);
expect_choice_field(f,1,EXTRACTPDF_FORM_FIELD_COMBO_BOX,UINT32_C(393216),2,1,0);expect_choice_option(f,1,0,"Tokyo","Tokyo");expect_choice_option(f,1,1,"Osaka","Osaka");expect_utf8_value(f,1,0,"Kyoto");
expect_choice_field(f,2,EXTRACTPDF_FORM_FIELD_LIST_BOX,0,3,1,0);expect_choice_option(f,2,1,"M","M");expect_option_value(f,2,0,1);
expect_choice_field(f,3,EXTRACTPDF_FORM_FIELD_LIST_BOX,UINT32_C(2097152),3,2,1);expect_choice_option(f,3,0,"r","Red");expect_choice_option(f,3,2,"b","Blue");expect_option_value(f,3,0,0);expect_option_value(f,3,1,2);extractpdf_drop_form(f);extractpdf_close(d);expect_extract_status(ACROFORM_BAD_OPT_PDF,EXTRACTPDF_ERROR_FORMAT);expect_extract_status(ACROFORM_BAD_I_PDF,EXTRACTPDF_ERROR_FORMAT);}

static void future_red(void){CHECK(0);}
static void run_case(const char *name)
{if(strcmp(name,"api-shell")==0)test_api_shell();else if(strcmp(name,"empty")==0)test_empty_and_non_pdf();else if(strcmp(name,"structure")==0)test_structure();else if(strcmp(name,"widgets")==0)test_widgets();else if(strcmp(name,"scalar-values")==0)test_scalar_values_and_full_api();else if(strcmp(name,"choice-values")==0)test_choice_values();else if(strcmp(name,"button-values")==0)future_red();else if(strcmp(name,"lifetime")==0)future_red();else CHECK(0);}
int main(int argc,char **argv)
{compile_surface();if(argc==3&&strcmp(argv[1],"--case")==0){run_case(argv[2]);return EXIT_SUCCESS;}CHECK(argc==1);test_api_shell();test_empty_and_non_pdf();test_structure();test_widgets();test_scalar_values_and_full_api();test_choice_values();future_red();return EXIT_SUCCESS;}
