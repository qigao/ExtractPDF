#include "pdf_form_common.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct extractpdf_form { extractpdf_pdf_form_model *model; };

static void extractpdf_form_reset_rect(extractpdf_rect *r){r->x0=0;r->y0=0;r->x1=0;r->y1=0;}
static const char *extractpdf_form_string_data(const extractpdf_pdf_form_model *m,const extractpdf_pdf_form_string *s){return s->present?m->strings+s->offset:NULL;}

extractpdf_status extractpdf_document_form(extractpdf_document *document,extractpdf_form **out_form)
{
    pdf_document *pdf; extractpdf_pdf_form_model *model=NULL; extractpdf_form *form;
    extractpdf_status status=EXTRACTPDF_OK; int caught_code=FZ_ERROR_NONE;
    if(out_form==NULL)return EXTRACTPDF_ERROR_ARGUMENT;*out_form=NULL;
    if(document==NULL||document->ctx==NULL||document->doc==NULL)return EXTRACTPDF_ERROR_ARGUMENT;
    pdf=pdf_document_from_fz_document(document->ctx,document->doc);if(pdf==NULL)return EXTRACTPDF_ERROR_UNSUPPORTED;
    fz_var(model);fz_var(status);fz_var(caught_code);
    fz_try(document->ctx){status=extractpdf_pdf_form_parse(document->ctx,pdf,&model);
        if(status==EXTRACTPDF_OK)status=extractpdf_pdf_form_reconcile_widgets(document->ctx,pdf,model);
        if(status==EXTRACTPDF_OK)status=extractpdf_pdf_form_materialize_scalar_values(document->ctx,pdf,model);}
    fz_catch(document->ctx){caught_code=fz_caught(document->ctx);fz_report_error(document->ctx);}
    if(caught_code!=FZ_ERROR_NONE){extractpdf_pdf_form_drop_model(model);return extractpdf_status_from_mupdf(caught_code);}
    if(status!=EXTRACTPDF_OK){extractpdf_pdf_form_drop_model(model);return status;}
    form=(extractpdf_form*)calloc(1,sizeof(*form));if(form==NULL){extractpdf_pdf_form_drop_model(model);return EXTRACTPDF_ERROR_NOMEM;}
    form->model=model;*out_form=form;return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_form_field_count(const extractpdf_form *form,size_t *out_count)
{if(out_count)*out_count=0;if(form==NULL||form->model==NULL||out_count==NULL)return EXTRACTPDF_ERROR_ARGUMENT;*out_count=form->model->field_count;return EXTRACTPDF_OK;}

extractpdf_status extractpdf_form_field_get_info(const extractpdf_form *form,size_t field_index,extractpdf_form_field_info *out_info)
{
    const extractpdf_pdf_form_field_internal *f;size_t min;
    if(out_info==NULL)return EXTRACTPDF_ERROR_ARGUMENT;min=offsetof(extractpdf_form_field_info,is_signed)+sizeof(out_info->is_signed);if(out_info->struct_size<min)return EXTRACTPDF_ERROR_ARGUMENT;
    out_info->type=EXTRACTPDF_FORM_FIELD_UNKNOWN;out_info->flags=0;out_info->value_presence=EXTRACTPDF_FORM_VALUE_NOT_APPLICABLE;out_info->value_count=0;out_info->option_count=0;out_info->widget_count=0;out_info->is_multiselect=0;out_info->is_signed=0;
    if(form==NULL||form->model==NULL||field_index>=form->model->field_count)return EXTRACTPDF_ERROR_ARGUMENT;
    f=&form->model->fields[field_index];out_info->type=f->type;out_info->flags=f->flags;out_info->value_presence=f->value_presence;out_info->value_count=f->value_count;out_info->option_count=f->option_count;out_info->widget_count=f->widget_count;out_info->is_multiselect=f->is_multiselect;out_info->is_signed=f->is_signed;return EXTRACTPDF_OK;
}

static extractpdf_status field_string(const extractpdf_form *form,size_t i,const extractpdf_pdf_form_string *s,const char **out,size_t *n)
{if(out)*out=NULL;if(n)*n=0;if(form==NULL||form->model==NULL||out==NULL||n==NULL||i>=form->model->field_count)return EXTRACTPDF_ERROR_ARGUMENT;if(!s->present)return EXTRACTPDF_OK;*out=extractpdf_form_string_data(form->model,s);*n=s->size;return EXTRACTPDF_OK;}
extractpdf_status extractpdf_form_field_name(const extractpdf_form *form,size_t i,const char **out,size_t *n){if(form==NULL||form->model==NULL||i>=form->model->field_count){if(out)*out=NULL;if(n)*n=0;return EXTRACTPDF_ERROR_ARGUMENT;}return field_string(form,i,&form->model->fields[i].name,out,n);}
extractpdf_status extractpdf_form_field_label(const extractpdf_form *form,size_t i,const char **out,size_t *n){if(form==NULL||form->model==NULL||i>=form->model->field_count){if(out)*out=NULL;if(n)*n=0;return EXTRACTPDF_ERROR_ARGUMENT;}return field_string(form,i,&form->model->fields[i].label,out,n);}

extractpdf_status extractpdf_form_field_value_get_info(const extractpdf_form *form,size_t fi,size_t vi,extractpdf_form_value_info *out)
{const extractpdf_pdf_form_field_internal *f;const extractpdf_pdf_form_value_internal *v;size_t min;if(out==NULL)return EXTRACTPDF_ERROR_ARGUMENT;min=offsetof(extractpdf_form_value_info,option_index)+sizeof(out->option_index);if(out->struct_size<min)return EXTRACTPDF_ERROR_ARGUMENT;out->kind=EXTRACTPDF_FORM_VALUE_UTF8;out->option_index=SIZE_MAX;if(form==NULL||form->model==NULL||fi>=form->model->field_count)return EXTRACTPDF_ERROR_ARGUMENT;f=&form->model->fields[fi];if(vi>=f->value_count)return EXTRACTPDF_ERROR_ARGUMENT;v=&form->model->values[f->first_value+vi];out->kind=v->kind;out->option_index=v->kind==EXTRACTPDF_FORM_VALUE_OPTION?v->option_index:SIZE_MAX;return EXTRACTPDF_OK;}
extractpdf_status extractpdf_form_field_value_utf8(const extractpdf_form *form,size_t fi,size_t vi,const char **out,size_t *n)
{const extractpdf_pdf_form_field_internal *f;const extractpdf_pdf_form_value_internal *v;if(out)*out=NULL;if(n)*n=0;if(form==NULL||form->model==NULL||out==NULL||n==NULL||fi>=form->model->field_count)return EXTRACTPDF_ERROR_ARGUMENT;f=&form->model->fields[fi];if(vi>=f->value_count)return EXTRACTPDF_ERROR_ARGUMENT;v=&form->model->values[f->first_value+vi];if(v->kind!=EXTRACTPDF_FORM_VALUE_UTF8)return EXTRACTPDF_ERROR_UNSUPPORTED;*out=extractpdf_form_string_data(form->model,&v->utf8);*n=v->utf8.size;return EXTRACTPDF_OK;}

extractpdf_status extractpdf_form_field_option_get_info(const extractpdf_form *form,size_t fi,size_t oi,extractpdf_form_option_info *out)
{const extractpdf_pdf_form_field_internal *f;const extractpdf_pdf_form_option_internal *o;size_t min;if(out==NULL)return EXTRACTPDF_ERROR_ARGUMENT;min=offsetof(extractpdf_form_option_info,kind)+sizeof(out->kind);if(out->struct_size<min)return EXTRACTPDF_ERROR_ARGUMENT;out->kind=EXTRACTPDF_FORM_OPTION_BUTTON_STATE;if(form==NULL||form->model==NULL||fi>=form->model->field_count)return EXTRACTPDF_ERROR_ARGUMENT;f=&form->model->fields[fi];if(oi>=f->option_count)return EXTRACTPDF_ERROR_ARGUMENT;o=&form->model->options[f->first_option+oi];out->kind=o->kind;return EXTRACTPDF_OK;}
static extractpdf_status option_string(const extractpdf_form *form,size_t fi,size_t oi,int display,const char **out,size_t *n)
{const extractpdf_pdf_form_field_internal *f;const extractpdf_pdf_form_option_internal *o;const extractpdf_pdf_form_string *s;if(out)*out=NULL;if(n)*n=0;if(form==NULL||form->model==NULL||out==NULL||n==NULL||fi>=form->model->field_count)return EXTRACTPDF_ERROR_ARGUMENT;f=&form->model->fields[fi];if(oi>=f->option_count)return EXTRACTPDF_ERROR_ARGUMENT;o=&form->model->options[f->first_option+oi];if(o->kind!=EXTRACTPDF_FORM_OPTION_CHOICE)return EXTRACTPDF_ERROR_UNSUPPORTED;s=display?&o->display_text:&o->export_text;*out=extractpdf_form_string_data(form->model,s);*n=s->size;return EXTRACTPDF_OK;}
extractpdf_status extractpdf_form_field_option_export(const extractpdf_form *f,size_t fi,size_t oi,const char **out,size_t *n){return option_string(f,fi,oi,0,out,n);}
extractpdf_status extractpdf_form_field_option_display(const extractpdf_form *f,size_t fi,size_t oi,const char **out,size_t *n){return option_string(f,fi,oi,1,out,n);}

extractpdf_status extractpdf_form_widget_count(const extractpdf_form *form,size_t *out){if(out)*out=0;if(form==NULL||form->model==NULL||out==NULL)return EXTRACTPDF_ERROR_ARGUMENT;*out=form->model->widget_count;return EXTRACTPDF_OK;}
extractpdf_status extractpdf_form_widget_get_info(const extractpdf_form *form,size_t wi,extractpdf_form_widget_info *out)
{const extractpdf_pdf_form_widget_internal *w;size_t min;if(out==NULL)return EXTRACTPDF_ERROR_ARGUMENT;min=offsetof(extractpdf_form_widget_info,button_option_index)+sizeof(out->button_option_index);if(out->struct_size<min)return EXTRACTPDF_ERROR_ARGUMENT;out->field_index=SIZE_MAX;out->page_index=-1;extractpdf_form_reset_rect(&out->bounds);out->flags=0;out->button_option_index=SIZE_MAX;if(form==NULL||form->model==NULL||wi>=form->model->widget_count)return EXTRACTPDF_ERROR_ARGUMENT;w=&form->model->widgets[wi];out->field_index=w->field_index;out->page_index=w->page_index;out->bounds=w->bounds;out->flags=w->flags;out->button_option_index=w->button_option_index;return EXTRACTPDF_OK;}

void extractpdf_drop_form(extractpdf_form *form){if(form==NULL)return;extractpdf_pdf_form_drop_model(form->model);free(form);}
