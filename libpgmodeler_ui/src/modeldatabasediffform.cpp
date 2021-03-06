/*
# PostgreSQL Database Modeler (pgModeler)
#
# Copyright 2006-2015 - Raphael Araújo e Silva <raphael@pgmodeler.com.br>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation version 3.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# The complete text of GPLv3 is at LICENSE file on source code root directory.
# Also, you can get the complete GNU General Public License at <http://www.gnu.org/licenses/>
*/

#include "modeldatabasediffform.h"
#include "configurationform.h"
#include "databaseimportform.h"
#include "pgmodeleruins.h"

ModelDatabaseDiffForm::ModelDatabaseDiffForm(QWidget *parent, Qt::WindowFlags f) : QDialog(parent, f)
{
  try
  {
    setupUi(this);

    import_helper=nullptr;
    diff_helper=nullptr;
    imported_model=nullptr;
    import_thread=diff_thread=export_thread=nullptr;
    import_item=diff_item=export_item=nullptr;
    export_conn=nullptr;
    process_paused=false;
    diff_progress=0;

    apply_on_server_ht=new HintTextWidget(apply_on_server_hint, this);
    apply_on_server_ht->setText(apply_on_server_rb->statusTip());

    store_in_file_ht=new HintTextWidget(store_in_file_hint, this);
    store_in_file_ht->setText(store_in_file_rb->statusTip());

    import_sys_objs_ht=new HintTextWidget(import_sys_objs_hint, this);
    import_sys_objs_ht->setText(import_sys_objs_chk->statusTip());

    import_ext_objs_ht=new HintTextWidget(import_ext_objs_hint, this);
    import_ext_objs_ht->setText(import_ext_objs_chk->statusTip());

    keep_cluster_objs_ht=new HintTextWidget(keep_cluster_objs_hint, this);
    keep_cluster_objs_ht->setText(keep_cluster_objs_chk->statusTip());

    trunc_tables_ht=new HintTextWidget(trunc_tables_hint, this);
    trunc_tables_ht->setText(trunc_tables_chk->statusTip());

    ignore_errors_ht=new HintTextWidget(ignore_errors_hint, this);
    ignore_errors_ht->setText(ignore_errors_chk->statusTip());

    force_recreation_ht=new HintTextWidget(force_recreation_hint, this);
    force_recreation_ht->setText(force_recreation_chk->statusTip());

    recreate_unmod_ht=new HintTextWidget(recreate_unmod_hint, this);
    recreate_unmod_ht->setText(recreate_unmod_chk->statusTip());

    cascade_mode_ht=new HintTextWidget(drop_cascade_hint, this);
    cascade_mode_ht->setText(cascade_mode_chk->statusTip());

    pgsql_ver_ht=new HintTextWidget(pgsql_ver_hint, this);
    pgsql_ver_ht->setText(pgsql_ver_chk->statusTip());

    keep_obj_perms_ht=new HintTextWidget(keep_obj_perms_hint, this);
    keep_obj_perms_ht->setText(keep_obj_perms_chk->statusTip());

    ignore_duplic_ht=new HintTextWidget(ignore_duplic_hint, this);
    ignore_duplic_ht->setText(ignore_duplic_chk->statusTip());

    reuse_sequences_ht=new HintTextWidget(reuse_sequences_hint, this);
    reuse_sequences_ht->setText(reuse_sequences_chk->statusTip());

    sqlcode_hl=new SyntaxHighlighter(sqlcode_txt, false);
    sqlcode_hl->loadConfiguration(GlobalAttributes::SQL_HIGHLIGHT_CONF_PATH);

    pgsql_ver_cmb->addItems(PgSQLVersions::ALL_VERSIONS);

    connect(cancel_btn, &QToolButton::clicked, [=](){ cancelOperation(true); });
    connect(pgsql_ver_chk, SIGNAL(toggled(bool)), pgsql_ver_cmb, SLOT(setEnabled(bool)));
    connect(connect_tb, SIGNAL(clicked()), this, SLOT(listDatabases()));
    connect(store_in_file_rb, SIGNAL(clicked()), this, SLOT(enableDiffMode()));
    connect(apply_on_server_rb, SIGNAL(clicked()), this, SLOT(enableDiffMode()));
    connect(file_edt, SIGNAL(textChanged(QString)), this, SLOT(enableDiffMode()));
    connect(database_cmb, SIGNAL(currentIndexChanged(int)), this, SLOT(enableDiffMode()));
    connect(generate_btn, SIGNAL(clicked()), this, SLOT(generateDiff()));
    connect(close_btn, SIGNAL(clicked()), this, SLOT(close()));
    connect(store_in_file_rb, SIGNAL(clicked(bool)), store_in_file_wgt, SLOT(setEnabled(bool)));
    connect(select_file_tb, SIGNAL(clicked()), this, SLOT(selectOutputFile()));
    connect(file_edt, SIGNAL(textChanged(QString)), this, SLOT(enableDiffMode()));
    connect(force_recreation_chk, SIGNAL(toggled(bool)), recreate_unmod_chk, SLOT(setEnabled(bool)));
  }
  catch(Exception &e)
  {
    throw Exception(e.getErrorMessage(),e.getErrorType(),__PRETTY_FUNCTION__,__FILE__,__LINE__,&e);
  }
}

ModelDatabaseDiffForm::~ModelDatabaseDiffForm(void)
{
	destroyThreads();
  destroyModel();
}

void ModelDatabaseDiffForm::setDatabaseModel(DatabaseModel *model)
{
	source_model=model;
}

void ModelDatabaseDiffForm::showEvent(QShowEvent *)
{
  ConnectionsConfigWidget::fillConnectionsComboBox(connections_cmb);
	connections_cmb->setEnabled(connections_cmb->count() > 0);
	connection_lbl->setEnabled(connections_cmb->isEnabled());
	connect_tb->setEnabled(connections_cmb->isEnabled());
	enableDiffMode();
	settings_tbw->setTabEnabled(1, false);
  settings_tbw->setTabEnabled(2, false);
}

void ModelDatabaseDiffForm::closeEvent(QCloseEvent *event)
{
	//Ignore the close event when the thread is running
  if(process_paused ||
     (import_thread && import_thread->isRunning()) ||
     (diff_thread && diff_thread->isRunning()) ||
     (export_thread && export_thread->isRunning()))
    event->ignore();
}

void ModelDatabaseDiffForm::createThreads(void)
{
	import_thread=new QThread;
	import_helper=new DatabaseImportHelper;
	import_helper->moveToThread(import_thread);

	diff_thread=new QThread;
	diff_helper=new ModelsDiffHelper;
	diff_helper->moveToThread(diff_thread);

  export_thread=new QThread;
  export_helper=new ModelExportHelper;
  export_helper->setIgnoredErrors({ QString("0A000") });
  export_helper->moveToThread(export_thread);

  connect(apply_on_server_btn, &QPushButton::clicked,
          [=](){ apply_on_server_btn->setEnabled(false);
                 exportDiff(false); });

  connect(import_thread, SIGNAL(started(void)), import_helper, SLOT(importDatabase()));
  connect(diff_thread, SIGNAL(started(void)), diff_helper, SLOT(diffModels()));
  connect(export_thread, SIGNAL(started(void)), export_helper, SLOT(exportToDBMS()));

  connect(import_helper, SIGNAL(s_importFinished(Exception)), this, SLOT(handleImportFinished(Exception)), Qt::QueuedConnection);
  connect(import_helper, SIGNAL(s_importAborted(Exception)), this, SLOT(captureThreadError(Exception)), Qt::QueuedConnection);
  connect(import_helper, SIGNAL(s_progressUpdated(int,QString,ObjectType)), this, SLOT(updateProgress(int,QString,ObjectType)), Qt::QueuedConnection);

  connect(diff_helper, SIGNAL(s_progressUpdated(int,QString,ObjectType)), this, SLOT(updateProgress(int,QString,ObjectType)));
  connect(diff_helper, SIGNAL(s_diffFinished()), this, SLOT(handleDiffFinished()), Qt::QueuedConnection);
  connect(diff_helper, SIGNAL(s_diffAborted(Exception)), this, SLOT(captureThreadError(Exception)), Qt::QueuedConnection);
  connect(diff_helper, SIGNAL(s_objectsDiffInfoGenerated(ObjectsDiffInfo)), this, SLOT(updateDiffInfo(ObjectsDiffInfo)), Qt::QueuedConnection);

  connect(export_helper, SIGNAL(s_errorIgnored(QString,QString, QString)), this, SLOT(handleErrorIgnored(QString,QString,QString)), Qt::QueuedConnection);
  connect(export_helper, SIGNAL(s_exportFinished()), this, SLOT(handleExportFinished()), Qt::QueuedConnection);
  connect(export_helper, SIGNAL(s_exportAborted(Exception)), this, SLOT(captureThreadError(Exception)), Qt::QueuedConnection);
  connect(export_helper, SIGNAL(s_progressUpdated(int,QString,ObjectType,QString)), this, SLOT(updateProgress(int,QString,ObjectType,QString)), Qt::QueuedConnection);

  connect(create_tb, SIGNAL(toggled(bool)), this, SLOT(filterDiffInfos()));
  connect(drop_tb, SIGNAL(toggled(bool)), this, SLOT(filterDiffInfos()));
  connect(alter_tb, SIGNAL(toggled(bool)), this, SLOT(filterDiffInfos()));
  connect(ignore_tb, SIGNAL(toggled(bool)), this, SLOT(filterDiffInfos()));
}

void ModelDatabaseDiffForm::destroyThreads(void)
{
  if(import_thread)
	{
		import_thread->quit();
		import_thread->wait();
		delete(import_thread);
		import_thread=nullptr;
    delete(import_helper);
    import_helper=nullptr;
	}

  if(diff_thread)
	{
		diff_thread->quit();
		diff_thread->wait();
		delete(diff_thread);
		diff_thread=nullptr;
    delete(diff_helper);
    diff_helper=nullptr;
	}

  if(export_thread)
  {
    export_thread->quit();
    export_thread->wait();

    if(export_conn)
    {
      delete(export_conn);
      export_conn=nullptr;
    }

    delete(export_thread);
    export_thread=nullptr;
    delete(export_helper);
    export_helper=nullptr;
  }
}

void ModelDatabaseDiffForm::destroyModel(void)
{
	if(imported_model)
		delete(imported_model);

	imported_model=nullptr;
}

void ModelDatabaseDiffForm::clearOutput(void)
{
	output_trw->clear();
	import_item=diff_item=export_item=nullptr;

	step_lbl->setText(trUtf8("Waiting process to start..."));
	step_ico_lbl->setPixmap(QPixmap());
	progress_lbl->setText(trUtf8("Waiting process to start..."));
	progress_ico_lbl->setPixmap(QPixmap());

	step_pb->setValue(0);
	progress_pb->setValue(0);

  create_tb->setText(QString("0"));
  alter_tb->setText(QString("0"));
  drop_tb->setText(QString("0"));
  ignore_tb->setText(QString("0"));
}

void ModelDatabaseDiffForm::listDatabases(void)
{
	try
	{
		Connection *conn=reinterpret_cast<Connection *>(connections_cmb->itemData(connections_cmb->currentIndex()).value<void *>());
		DatabaseImportHelper imp_helper;

		imp_helper.setConnection(*conn);
    DatabaseImportForm::listDatabases(imp_helper, database_cmb);

		database_cmb->setEnabled(database_cmb->count() > 0);
		database_lbl->setEnabled(database_cmb->isEnabled());
	}
	catch(Exception &e)
	{
		throw Exception(e.getErrorMessage(), e.getErrorType(),__PRETTY_FUNCTION__,__FILE__,__LINE__, &e);
	}
}

void ModelDatabaseDiffForm::enableDiffMode(void)
{
	store_in_file_wgt->setEnabled(store_in_file_rb->isChecked());

  generate_btn->setEnabled(database_cmb->currentIndex() > 0 &&
													 ((store_in_file_rb->isChecked() && !file_edt->text().isEmpty()) ||
														(apply_on_server_rb->isChecked())));
}

void ModelDatabaseDiffForm::generateDiff(void)
{
  destroyModel();
	clearOutput();
	createThreads();
  importDatabase();

  buttons_wgt->setEnabled(false);
	cancel_btn->setEnabled(true);
	generate_btn->setEnabled(false);

	settings_tbw->setTabEnabled(0, false);  
	settings_tbw->setTabEnabled(1, true);
  settings_tbw->setTabEnabled(2, false);
	settings_tbw->setCurrentIndex(1);
}

void ModelDatabaseDiffForm::importDatabase(void)
{
	try
	{
		Connection conn=(*reinterpret_cast<Connection *>(connections_cmb->itemData(connections_cmb->currentIndex()).value<void *>())), conn1;
		map<ObjectType, vector<unsigned>> obj_oids;
		map<unsigned, vector<unsigned>> col_oids;
		Catalog catalog;

		conn1=conn;
		step_lbl->setText(trUtf8("Importing database <strong>%1</strong>...").arg(database_cmb->currentText()));
		step_ico_lbl->setPixmap(QPixmap(QString(":/icones/icones/import.png")));

    import_item=PgModelerUiNS::createOutputTreeItem(output_trw, step_lbl->text(), *step_ico_lbl->pixmap(), nullptr);

		conn.switchToDatabase(database_cmb->currentText());
    pgsql_ver=conn.getPgSQLVersion(true);

		catalog.setConnection(conn);

    //The import process will exclude built-in array array types, system and extension objects
    catalog.setFilter(Catalog::LIST_ALL_OBJS | Catalog::EXCL_BUILTIN_ARRAY_TYPES |
                      Catalog::EXCL_EXTENSION_OBJS | Catalog::EXCL_SYSTEM_OBJS);
    catalog.getObjectsOIDs(obj_oids, col_oids, {{ParsersAttributes::FILTER_TABLE_TYPES, ParsersAttributes::_TRUE_}});
		obj_oids[OBJ_DATABASE].push_back(database_cmb->currentData().value<unsigned>());

		imported_model=new DatabaseModel;
		imported_model->createSystemObjects(true);	

		import_helper->setConnection(conn1);
		import_helper->setSelectedOIDs(imported_model, obj_oids, col_oids);
		import_helper->setCurrentDatabase(database_cmb->currentText());
		import_helper->setImportOptions(import_sys_objs_chk->isChecked(), import_ext_objs_chk->isChecked(), true, ignore_errors_chk->isChecked(), false, false);

		import_thread->start();
	}
	catch(Exception &e)
	{
		throw Exception(e.getErrorMessage(), e.getErrorType(),__PRETTY_FUNCTION__,__FILE__,__LINE__, &e);
	}
}

void ModelDatabaseDiffForm::diffModels(void)
{
	step_lbl->setText(trUtf8("Comparing the model <strong>%1</strong> and database <strong>%2</strong>...")
										.arg(source_model->getName())
										.arg(imported_model->getName()));
  step_ico_lbl->setPixmap(QPixmap(QString(":/icones/icones/diff.png")));

	output_trw->collapseItem(import_item);
	diff_progress=step_pb->value();

  diff_item=PgModelerUiNS::createOutputTreeItem(output_trw, step_lbl->text(), *step_ico_lbl->pixmap(), nullptr);

  diff_helper->setDiffOptions(keep_cluster_objs_chk->isChecked(),
                              cascade_mode_chk->isChecked(),
                              trunc_tables_chk->isChecked(),
                              force_recreation_chk->isChecked(),
                              recreate_unmod_chk->isChecked(),
                              keep_obj_perms_chk->isChecked(),
                              reuse_sequences_chk->isChecked());

  diff_helper->setModels(source_model, imported_model);

  if(pgsql_ver_chk->isChecked())
    diff_helper->setPgSQLVersion(pgsql_ver_cmb->currentText());
  else
    diff_helper->setPgSQLVersion(pgsql_ver);

  diff_thread->start();
}

void ModelDatabaseDiffForm::exportDiff(bool confirm)
{
  Messagebox msg_box;

  if(confirm)
    msg_box.show(trUtf8("Confirmation"),
                 trUtf8(" <strong>WARNING:</strong> The generated diff is ready to be exported! Once started this process will cause irreversible changes on the database. Do you really want to proceed?"),
                 Messagebox::ALERT_ICON, Messagebox::ALL_BUTTONS,
                 trUtf8("Apply diff"), trUtf8("Preview diff"), QString(),
                 QString(":/icones/icones/diff.png"), QString(":/icones/icones/codigosql.png"));

  if(!confirm || msg_box.result()==QDialog::Accepted)
  {
    settings_tbw->setCurrentIndex(1);
    apply_on_server_btn->setEnabled(true);

    step_lbl->setText(trUtf8("Exporting diff to database <strong>%1</strong>...")
                      .arg(imported_model->getName()));
    step_ico_lbl->setPixmap(QPixmap(QString(":/icones/icones/exportar.png")));

    output_trw->collapseItem(diff_item);
    diff_progress=step_pb->value();
    export_item=PgModelerUiNS::createOutputTreeItem(output_trw, step_lbl->text(), *step_ico_lbl->pixmap(), nullptr);

    export_conn=new Connection;
    *export_conn=*reinterpret_cast<Connection *>(connections_cmb->itemData(connections_cmb->currentIndex()).value<void *>());
    export_helper->setExportToDBMSParams(sqlcode_txt->toPlainText(), export_conn,
                                         database_cmb->currentText(), ignore_duplic_chk->isChecked());
    export_thread->start();
  }
  else if(msg_box.isCancelled())
    cancelOperation(true);
  else
  {
    process_paused=true;
    settings_tbw->setCurrentIndex(2);
    apply_on_server_btn->setVisible(true);
    output_trw->collapseItem(diff_item);
    PgModelerUiNS::createOutputTreeItem(output_trw,
                                        trUtf8("Diff process paused. Waiting user action..."),
                                        QPixmap(QString(":/icones/icones/msgbox_alerta.png")), nullptr);
  }
}

void ModelDatabaseDiffForm::filterDiffInfos(void)
{
  QToolButton *btn=dynamic_cast<QToolButton *>(sender());
  map<QToolButton *, unsigned> diff_types={ {create_tb, ObjectsDiffInfo::CREATE_OBJECT},
                                            {drop_tb, ObjectsDiffInfo::DROP_OBJECT},
                                            {alter_tb, ObjectsDiffInfo::ALTER_OBJECT},
                                            {ignore_tb, ObjectsDiffInfo::IGNORE_OBJECT}};

  for(int i=0; i < diff_item->childCount(); i++)
  {
    if(diff_item->child(i)->data(0, Qt::UserRole).toUInt()==diff_types[btn])
      output_trw->setItemHidden(diff_item->child(i), !btn->isChecked());
  }
}

void ModelDatabaseDiffForm::resetButtons(void)
{
  buttons_wgt->setEnabled(true);
	cancel_btn->setEnabled(false);
  settings_tbw->setTabEnabled(0, true);
  apply_on_server_btn->setVisible(false);
  enableDiffMode();
}

void ModelDatabaseDiffForm::saveDiffToFile(void)
{
  if(!sqlcode_txt->toPlainText().isEmpty())
  {
    QFile output;

    step_lbl->setText(trUtf8("Saving diff to file <strong>%1</strong>").arg(file_edt->text()));
    step_ico_lbl->setPixmap(QPixmap(QString(":/icones/icones/salvar.png")));
    import_item=PgModelerUiNS::createOutputTreeItem(output_trw, step_lbl->text(), *step_ico_lbl->pixmap(), nullptr);
    step_pb->setValue(90);
    progress_pb->setValue(100);

    output.setFileName(file_edt->text());

    if(!output.open(QFile::WriteOnly))
      captureThreadError(Exception(Exception::getErrorMessage(ERR_FILE_DIR_NOT_WRITTEN).arg(file_edt->text()),
                                   ERR_FILE_DIR_NOT_WRITTEN, __PRETTY_FUNCTION__,__FILE__,__LINE__));

    output.write(sqlcode_txt->toPlainText().toUtf8());
    output.close();
  }

  finishDiff();
}

void ModelDatabaseDiffForm::finishDiff(void)
{
  cancelOperation(false);

  step_lbl->setText(trUtf8("Diff process sucessfully ended!"));
  progress_lbl->setText(trUtf8("No operations left."));

  step_ico_lbl->setPixmap(QPixmap(QString(":/icones/icones/msgbox_info.png")));
  progress_ico_lbl->setPixmap(QPixmap(QString(":/icones/icones/msgbox_info.png")));

  import_item=PgModelerUiNS::createOutputTreeItem(output_trw, step_lbl->text(), *step_ico_lbl->pixmap(), nullptr);
  step_pb->setValue(100);
  progress_pb->setValue(100);
}

void ModelDatabaseDiffForm::cancelOperation(bool cancel_by_user)
{
  if(cancel_by_user)
  {
    step_lbl->setText(trUtf8("Operation cancelled by the user."));
    progress_lbl->setText(trUtf8("No operations left."));

    step_ico_lbl->setPixmap(QPixmap(QString(":/icones/icones/msgbox_alerta.png")));
    progress_ico_lbl->setPixmap(QPixmap(QString(":/icones/icones/msgbox_alerta.png")));

    PgModelerUiNS::createOutputTreeItem(output_trw, step_lbl->text(), *step_ico_lbl->pixmap(), nullptr);
  }

  if(import_helper)
   import_helper->cancelImport();

  if(diff_helper)
   diff_helper->cancelDiff();

  if(export_helper)
   export_helper->cancelExport();

  destroyThreads();
  resetButtons();
  process_paused=false;
}

void ModelDatabaseDiffForm::captureThreadError(Exception e)
{
  QTreeWidgetItem *item=nullptr;

  cancelOperation(false);
	progress_lbl->setText(trUtf8("Process aborted due to errors!"));
	progress_ico_lbl->setPixmap(QPixmap(QString(":/icones/icones/msgbox_erro.png")));

  item=PgModelerUiNS::createOutputTreeItem(output_trw, PgModelerUiNS::formatMessage(e.getErrorMessage()), *progress_ico_lbl->pixmap(), nullptr, true);

  if(!e.getExtraInfo().isEmpty())
    PgModelerUiNS::createOutputTreeItem(output_trw, PgModelerUiNS::formatMessage(e.getExtraInfo()), *progress_ico_lbl->pixmap(), item, true);

  throw Exception(e.getErrorMessage(), e.getErrorType(),__PRETTY_FUNCTION__,__FILE__,__LINE__, &e);
}

void ModelDatabaseDiffForm::handleImportFinished(Exception e)
{
	if(!e.getErrorMessage().isEmpty())
	{
		Messagebox msgbox;
		msgbox.show(e, e.getErrorMessage(), Messagebox::ALERT_ICON);
	}

  step_pb->setValue(30);
  import_thread->quit();
  import_thread->wait();
  diffModels();
}

void ModelDatabaseDiffForm::handleDiffFinished(void)
{
  sqlcode_txt->setPlainText(diff_helper->getDiffDefinition()); 
  settings_tbw->setTabEnabled(2, true);
  diff_thread->quit();
  diff_thread->wait();

  if(store_in_file_rb->isChecked())
    saveDiffToFile();
  else if(!sqlcode_txt->toPlainText().isEmpty())
    exportDiff();
  else
    finishDiff();

  if(sqlcode_txt->toPlainText().isEmpty())
    sqlcode_txt->setText(trUtf8("-- No differences were detected between model and database. --"));
}

void ModelDatabaseDiffForm::handleExportFinished(void)
{
  export_thread->quit();
  export_thread->wait();
  listDatabases();
  finishDiff();
}

void ModelDatabaseDiffForm::handleErrorIgnored(QString err_code, QString err_msg, QString cmd)
{
  QTreeWidgetItem *item=nullptr;

  item=PgModelerUiNS::createOutputTreeItem(output_trw, trUtf8("Error code <strong>%1</strong> found and ignored. Proceeding with export.").arg(err_code),
                 QPixmap(QString(":/icones/icones/msgbox_alerta.png")),
                 export_item, false, false);

  PgModelerUiNS::createOutputTreeItem(output_trw, PgModelerUiNS::formatMessage(err_msg),
                 QPixmap(QString(":/icones/icones/msgbox_alerta.png")),
                 item, true, false);

  PgModelerUiNS::createOutputTreeItem(output_trw, cmd,
                 QPixmap(),
                 item, true, false);
}

void ModelDatabaseDiffForm::updateProgress(int progress, QString msg, ObjectType obj_type, QString cmd)
{
  msg=PgModelerUiNS::formatMessage(msg);

  if(import_thread && import_thread->isRunning())
	{
		if(progress > 90)
			step_pb->setValue(step_pb->value() + 5);

    PgModelerUiNS::createOutputTreeItem(output_trw, msg,
                   QPixmap(QString(":/icones/icones/") + BaseObject::getSchemaName(obj_type) + QString(".png")),
                   import_item);
	}
  else if(diff_thread && diff_thread->isRunning())
  {
    if(progress >= 90 && obj_type==BASE_OBJECT)
    {
      PgModelerUiNS::createOutputTreeItem(output_trw, msg,
                                          QPixmap(QString(":/icones/icones/msgbox_info.png")),
                                          diff_item);
    }

    step_pb->setValue(diff_progress + (progress/3));
  }
  else if(export_thread && export_thread->isRunning())
  {
    QTreeWidgetItem *item=nullptr;
    QPixmap ico;
    step_pb->setValue(diff_progress + (progress/3));

    if(obj_type==BASE_OBJECT)
      ico=QPixmap(QString(":/icones/icones/codigosql.png"));
    else
      ico=QPixmap(QString(":/icones/icones/") + BaseObject::getSchemaName(obj_type) + QString(".png"));

    item=PgModelerUiNS::createOutputTreeItem(output_trw, msg, ico, export_item, false, false);

    if(!cmd.isEmpty())
      PgModelerUiNS::createOutputTreeItem(output_trw, cmd, QPixmap(), item, true, false);
  }

  progress_lbl->setText(msg);
	progress_pb->setValue(progress);

	if(obj_type!=BASE_OBJECT)
		progress_ico_lbl->setPixmap(QPixmap(QString(":/icones/icones/") + BaseObject::getSchemaName(obj_type) + QString(".png")));
	else
		progress_ico_lbl->setPixmap(QPixmap(QString(":/icones/icones/msgbox_info.png")));

	this->repaint();
}

void ModelDatabaseDiffForm::updateDiffInfo(ObjectsDiffInfo diff_info)
{
  map<unsigned, QToolButton *> buttons={ {ObjectsDiffInfo::CREATE_OBJECT, create_tb},
                                         {ObjectsDiffInfo::DROP_OBJECT,   drop_tb},
                                         {ObjectsDiffInfo::ALTER_OBJECT,  alter_tb},
                                         {ObjectsDiffInfo::IGNORE_OBJECT, ignore_tb} };

	unsigned diff_type=diff_info.getDiffType();
  QToolButton *btn=buttons[diff_type];
  QTreeWidgetItem *item=nullptr;

  item=PgModelerUiNS::createOutputTreeItem(output_trw,
                                           PgModelerUiNS::formatMessage(diff_info.getInfoMessage()),
                                           QPixmap(QString(":/icones/icones/%1.png").arg(diff_info.getObject()->getSchemaName())), diff_item);
  item->setData(0, Qt::UserRole, diff_info.getDiffType());

  if(diff_helper)
    btn->setText(QString::number(diff_helper->getDiffTypeCount(diff_type)));

  output_trw->setItemHidden(item, !btn->isChecked());
}

void ModelDatabaseDiffForm::selectOutputFile(void)
{
  QFileDialog file_dlg;

  file_dlg.setWindowTitle(trUtf8("Save diff as..."));
  file_dlg.setFileMode(QFileDialog::AnyFile);
  file_dlg.setAcceptMode(QFileDialog::AcceptSave);
  file_dlg.setModal(true);
  file_dlg.setNameFilter(trUtf8("SQL code (*.sql);;All files (*.*)"));
  file_dlg.selectFile(source_model->getName() + QString("-diff.sql"));

  if(file_dlg.exec()==QFileDialog::Accepted)
  {
    QString file;

    if(!file_dlg.selectedFiles().isEmpty())
      file = file_dlg.selectedFiles().at(0);

    file_edt->setText(file);
  }
}

